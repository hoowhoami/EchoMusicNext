//! Independent daily/style captures share this layout. Keep navigation, rights,
//! quality, MV and recommendation facts; do not collapse them to a title/hash.
use crate::{
    domain::{ArtistRef, SongSummary, cover_url, discovery::*},
    error::ApiError,
    util::catalog_rights::WireTrans,
};
use serde::Deserialize;
#[derive(Default, Deserialize)]
pub struct Song {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    time_length: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    sizable_cover: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    relate_goods: Option<serde_json::Value>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singerinfo: Vec<Singer>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    ori_audio_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    official_songname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    suffix_audio_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_audio_remark: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    filename: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    language: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_date: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_original: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    has_accompany: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pay_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_process: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    old_cpy: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_128: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_192: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_320: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_flac: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_ape: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash_other: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_128: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_192: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_flac: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_ape: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    filesize_other: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    timelength_320: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    bitrate: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    extname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    mv_hash: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    mv_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    tags: Vec<Tag>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    ips_tags: IpTags,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rec_copy_write: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rec_sub_copy_write: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rank_label: Option<String>,
}
#[derive(Deserialize)]
struct Singer {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_publish: Option<String>,
}
#[derive(Deserialize)]
struct Tag {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tag_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    parent_id: String,
}
#[derive(Deserialize)]
struct IpTag {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    ip_id: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    name: Option<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pid: Option<String>,
}
// Daily captures contain both a list and the exact empty-object variant.
#[derive(Deserialize)]
#[serde(untagged)]
enum IpTags {
    List(Vec<IpTag>),
    Entry(IpTag),
}
impl IpTags {
    fn into_vec(self) -> Vec<IpTag> {
        match self {
            Self::List(v) => v,
            Self::Entry(tag) => {
                if tag.ip_id.is_none() && tag.name.is_none() && tag.pid.is_none() {
                    Vec::new()
                } else {
                    vec![tag]
                }
            }
        }
    }
}
pub fn decimal(value: &str) -> Result<u64, ApiError> {
    value.parse().map_err(|_| ApiError::decode())
}
impl Song {
    pub fn convert(self) -> Result<DiscoverySong, ApiError> {
        let duration = self.time_length.and_then(|v| v.checked_mul(1000));
        let (rights, mut resources) = self.trans_param.convert()?;
        for (quality, hash, size, seconds, ext) in [
            (
                "128",
                self.hash_128,
                self.filesize_128,
                self.time_length,
                Some(self.extname),
            ),
            ("192", self.hash_192, self.filesize_192, None, None),
            (
                "320",
                self.hash_320,
                self.filesize_320,
                self.timelength_320,
                None,
            ),
            ("flac", self.hash_flac, self.filesize_flac, None, None),
            ("ape", self.hash_ape, self.filesize_ape, None, None),
            ("other", self.hash_other, self.filesize_other, None, None),
        ] {
            resources.push(AudioResource {
                quality: quality.into(),
                hash: Some(hash),
                size_bytes: size,
                duration_ms: seconds.and_then(|v| v.checked_mul(1000)),
                bit_rate_bps: if quality == "128" {
                    self.bitrate.and_then(|v| v.checked_mul(1000))
                } else {
                    None
                },
                extension: ext,
                privilege: None,
                payment_type: None,
                failure_code: None,
                download_status: None,
            });
        }
        let artists = self
            .singerinfo
            .into_iter()
            .map(|s| {
                Ok(ArtistDetail {
                    id: decimal(&s.id).ok(),
                    name: s.name,
                    avatar_url: None,
                    publication_code: s
                        .is_publish
                        .and_then(|v| decimal(&v).ok())
                        .and_then(|v| u32::try_from(v).ok()),
                })
            })
            .collect::<Result<Vec<_>, ApiError>>()?;
        let videos = if self.mv_hash.is_empty() {
            Vec::new()
        } else {
            vec![VideoReference {
                id: None,
                hash: Some(self.mv_hash),
                kind: self.mv_type,
                track: None,
                duration_ms: None,
                size_bytes: None,
            }]
        };
        Ok(DiscoverySong {
            song: SongSummary {
                id: self.album_audio_id,
                hash: self.hash,
                title: self.songname,
                artists: artists
                    .iter()
                    .map(|s| ArtistRef {
                        id: s.id,
                        name: s.name.clone(),
                    })
                    .collect(),
                album_id: self.album_id,
                album_title: Some(self.album_name),
                duration_ms: duration,
                cover_url: cover_url(self.sizable_cover),
                privilege: self.privilege,
            },
            metadata: SongMetadata {
                related_goods_extension: self.relate_goods,
                provider_extension: None,
                accompaniment_code: self.has_accompany,
                audio_id: self.songid,
                original_title: Some(self.ori_audio_name),
                official_title: Some(self.official_songname),
                title_suffix: Some(self.suffix_audio_name),
                album_audio_remark: self.album_audio_remark,
                source_file_name: self.filename,
                language: Some(self.language),
                published_at: Some(self.publish_date),
                publication_code: self.is_publish,
                originality_code: self.is_original,
                has_accompaniment: self.has_accompany.and_then(crate::util::numeric::boolean),
                payment_type: self.pay_type,
                failure_code: self.fail_process,
                old_copyright_code: self.old_cpy,
                rights,
                resources,
                videos,
                tags: self
                    .tags
                    .into_iter()
                    .map(|t| SongTag {
                        id: Some(t.tag_id),
                        name: Some(t.tag_name),
                        parent_id: Some(t.parent_id),
                    })
                    .collect(),
                ip_tags: self
                    .ips_tags
                    .into_vec()
                    .into_iter()
                    .map(|t| SongTag {
                        id: t.ip_id,
                        name: t.name,
                        parent_id: t.pid,
                    })
                    .collect(),
                recommendation: RecommendationText {
                    headline: self.rec_copy_write.unwrap_or_default(),
                    detail: self.rec_sub_copy_write.unwrap_or_default(),
                    rank_label: self.rank_label,
                    curator: None,
                    artwork_url: None,
                },
                ranking: None,
                artists,
                related_versions: Vec::new(),
            },
        })
    }
}

impl Default for IpTags {
    fn default() -> Self {
        Self::List(Vec::new())
    }
}
