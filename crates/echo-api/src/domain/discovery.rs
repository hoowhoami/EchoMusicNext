//! Catalog facts used by EchoMusic song cards, navigation and playback. Rights
//! codes are facts for display; they are not a fabricated playback authorization.
use super::SongSummary;
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, ops::Deref};
#[derive(Debug, Serialize)]
pub struct DiscoverySong {
    #[serde(flatten)]
    pub song: SongSummary,
    pub metadata: SongMetadata,
}
impl Deref for DiscoverySong {
    type Target = SongSummary;
    fn deref(&self) -> &SongSummary {
        &self.song
    }
}
#[derive(Debug, Serialize)]
pub struct SongMetadata {
    pub audio_id: Option<u64>,
    pub original_title: Option<String>,
    pub official_title: Option<String>,
    pub title_suffix: Option<String>,
    pub album_audio_remark: String,
    pub source_file_name: String,
    pub language: Option<String>,
    pub published_at: Option<String>,
    pub publication_code: Option<u32>,
    pub originality_code: Option<u32>,
    pub has_accompaniment: Option<bool>,
    pub accompaniment_code: Option<u32>,
    /// Separate unmodeled provider data, not a replacement for mapped catalog facts.
    pub related_goods_extension: Option<serde_json::Value>,
    /// Provider keys kept verbatim where the domain has no established
    /// semantics yet; never a substitute for the explicitly mapped facts.
    pub provider_extension: Option<serde_json::Value>,
    pub payment_type: Option<u32>,
    pub failure_code: Option<u32>,
    pub old_copyright_code: Option<u32>,
    pub rights: CatalogRights,
    pub resources: Vec<AudioResource>,
    pub videos: Vec<VideoReference>,
    pub tags: Vec<SongTag>,
    pub ip_tags: Vec<SongTag>,
    pub recommendation: RecommendationText,
    pub ranking: Option<RankingPosition>,
    pub artists: Vec<ArtistDetail>,
    pub related_versions: Vec<RelatedVersion>,
}
#[derive(Debug, Serialize)]
pub struct AudioResource {
    pub quality: String,
    pub hash: Option<String>,
    pub size_bytes: Option<u64>,
    pub duration_ms: Option<u64>,
    pub bit_rate_bps: Option<u64>,
    pub extension: Option<String>,
    pub privilege: Option<u32>,
    pub payment_type: Option<u32>,
    pub failure_code: Option<u32>,
    pub download_status: Option<u32>,
}

/// Minimal catalog facts shared by song rows whose samples expose payment,
/// quality resources and rights without the full discovery metadata bundle.
/// Rights and resources stay available to badges, filters and queue snapshots.
#[derive(Debug, Serialize)]
pub struct CatalogSongFacts {
    pub payment_type: Option<u32>,
    pub failure_code: Option<u32>,
    pub old_copyright_code: Option<u32>,
    pub originality_code: Option<u32>,
    pub rights: Option<CatalogRights>,
    pub resources: Vec<AudioResource>,
}
#[derive(Debug, Serialize)]
pub struct VideoReference {
    pub id: Option<String>,
    pub hash: Option<String>,
    pub kind: Option<u32>,
    pub track: Option<String>,
    pub duration_ms: Option<u64>,
    pub size_bytes: Option<u64>,
}
#[derive(Clone, Debug, Serialize)]
pub struct ArtistDetail {
    pub id: Option<u64>,
    pub name: String,
    pub avatar_url: Option<String>,
    pub publication_code: Option<u32>,
}
#[derive(Debug, Serialize)]
pub struct SongTag {
    pub id: Option<String>,
    pub name: Option<String>,
    pub parent_id: Option<String>,
}
#[derive(Debug, Serialize)]
pub struct RecommendationText {
    pub headline: String,
    pub detail: String,
    pub rank_label: Option<String>,
}
#[derive(Debug, Serialize)]
pub struct RankingPosition {
    pub rank_id: String,
    pub issue: String,
    pub rank_cid: Option<u64>,
    pub position: Option<i64>,
    pub previous_position: Option<i64>,
    pub original_position: Option<i64>,
    pub previous_original_position: Option<i64>,
    pub change: Option<i64>,
    pub published_at: String,
}
#[derive(Debug, Serialize)]
pub struct CatalogRights {
    pub quality_map: Option<crate::module::privilege_lite::QualityMap>,
    pub classification: Option<BTreeMap<String, Option<u64>>>,
    pub ip_map: Option<EncodedMap>,
    pub copyright_attributes: Option<u64>,
    pub copyright_scope: Option<String>,
    pub copyright_grade: Option<u32>,
    pub copyright_level: Option<u32>,
    pub display_code: Option<u64>,
    pub display_rate: Option<u32>,
    pub payment_template: Option<u32>,
    pub blocked_applications: Option<String>,
    pub originality_code: Option<u32>,
    pub advance_code: Option<u32>,
    pub preview: Option<crate::module::privilege_lite::PreviewRange>,
    pub multitrack_hash: Option<String>,
    pub cover_url: Option<String>,
    pub audio_privilege: Option<u32>,
    pub free_for_ad: Option<u32>,
    pub free_limited: Option<u32>,
    pub voice_ad: Option<VoiceAd>,
}
#[derive(Debug, Serialize)]
pub struct VoiceAd {
    pub start_ms: Option<u64>,
    pub size_bytes: Option<u64>,
    pub hash: Option<String>,
}
#[derive(Debug, Deserialize, Serialize)]
pub struct EncodedMap {
    #[serde(
        default,
        deserialize_with = "crate::util::numeric::optional_unsigned_json"
    )]
    pub attr0: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub bits: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct RelatedVersion {
    pub album_audio_id: String,
    pub remark: String,
    pub kind: Option<u32>,
}
