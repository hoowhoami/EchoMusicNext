use serde::Serialize;

/// Verification configuration and supported login challenge kinds.
/// Session identifiers stay in memory; no Debug output or arbitrary provider URL.
#[derive(Serialize)]
pub struct VerificationConfiguration {
    pub provider: &'static str,
    pub vendor: &'static str,
    pub app_id: String,
    pub display_message: String,
    pub verification_type: u32,
    pub available_types: Vec<u32>,
    pub business_id: Option<u64>,
    pub session_id: Option<String>,
    pub partner_status: std::collections::BTreeMap<String, i64>,
    /// Preserve the observed protocol flag without inferring undocumented meaning.
    pub protocol_flag: Option<u32>,
}

#[derive(Debug, Serialize)]
pub struct ArtistRef {
    pub id: Option<u64>,
    pub name: String,
}

#[derive(Debug, Serialize)]
pub struct SongSummary {
    pub id: String,
    pub hash: String,
    pub title: String,
    pub artists: Vec<ArtistRef>,
    pub album_id: String,
    pub album_title: Option<String>,
    pub duration_ms: Option<u64>,
    pub cover_url: String,
    pub privilege: Option<u32>,
}

#[derive(Debug, Serialize)]
pub struct Page<T> {
    pub page: Option<u32>,
    pub page_size: Option<u32>,
    pub total: Option<u64>,
    pub items: Vec<T>,
}

#[derive(Debug, Serialize)]
pub struct AlbumSummary {
    pub id: Option<u64>,
    pub title: String,
    pub artists: Vec<ArtistRef>,
    pub cover_url: String,
    pub song_count: Option<u32>,
    pub published_at: String,
}

#[derive(Debug, Serialize)]
pub struct ArtistSummary {
    pub id: Option<u64>,
    pub name: String,
    pub avatar_url: String,
    pub album_count: Option<u64>,
    pub song_count: Option<u64>,
    pub fans: Option<u64>,
}

#[derive(Debug, Serialize)]
pub struct PlaylistSummary {
    pub id: Option<u64>,
    pub global_id: String,
    pub title: String,
    pub creator_id: String,
    pub creator_name: String,
    pub cover_url: String,
    pub song_count: Option<u32>,
    pub play_count: Option<u64>,
}

#[derive(Debug, Serialize)]
pub struct LyricHit {
    pub song: SongSummary,
    pub metadata: crate::domain::discovery::CatalogSongFacts,
    pub lyric_id: Option<u64>,
    pub excerpt: String,
}

#[derive(Debug, Serialize)]
pub struct MvSummary {
    pub id: Option<u64>,
    pub hash: String,
    pub title: String,
    pub artists: Vec<ArtistRef>,
    pub duration_ms: Option<u64>,
    pub thumbnail_file: String,
}

/// Song-search-specific display facts. Unrelated endpoints keep their own field models.
#[derive(Debug, Serialize)]
pub struct SearchSong {
    /// Audioid, distinct from MixSongID (album_audio_id).
    pub audio_id: Option<u64>,
    #[serde(flatten)]
    pub song: SongSummary,
    pub metadata: crate::domain::discovery::CatalogSongFacts,
    pub original: Option<bool>,
    pub original_code: Option<u32>,
}
impl std::ops::Deref for SearchSong {
    type Target = SongSummary;
    fn deref(&self) -> &Self::Target {
        &self.song
    }
}

#[derive(Debug, Serialize)]
#[serde(tag = "kind", content = "page", rename_all = "snake_case")]
pub enum SearchResults {
    Songs(Page<SearchSong>),
    Albums(Page<AlbumSummary>),
    Artists(Page<ArtistSummary>),
    Playlists(Page<PlaylistSummary>),
    Lyrics(Page<LyricHit>),
    Mvs(Page<MvSummary>),
}

#[derive(Debug, Serialize)]
pub struct HotKeyword {
    pub keyword: String,
    pub reason: String,
    pub kind: Option<u32>,
    pub icon: Option<u32>,
    pub jump_url: String,
    pub json_url: String,
}

#[derive(Debug, Serialize)]
pub struct HotSearchGroup {
    pub title: String,
    pub keywords: Vec<HotKeyword>,
}

#[derive(Debug, Serialize)]
pub struct Profile {
    pub id: u64,
    pub nickname: String,
    pub avatar_url: String,
    pub gender: Option<u32>,
    pub vip_type: Option<u32>,
    pub follows: Option<u64>,
    pub fans: Option<u64>,
    pub biography: String,
    pub friends: Option<u64>,
    pub grade: Option<u32>,
    pub listening_minutes: Option<u64>,
    pub visitors: Option<u64>,
    pub registered_at_seconds: Option<u64>,
    pub province: String,
    pub city: String,
    pub ip_location: String,
    pub birthday: String,
    pub identity: ProfileIdentity,
}

/// Account identity facts independent of widget presentation.
#[derive(Debug, Default, Serialize)]
pub struct ProfileIdentity {
    pub actor_code: Option<u32>,
    pub business_code: Option<u32>,
    pub star_code: Option<u32>,
    pub singer_code: Option<u32>,
    pub legacy_star_code: Option<i32>,
    pub talent_bits: Option<u32>,
    pub actor: bool,
    pub business: bool,
    pub star: bool,
    pub singer: bool,
    pub authorizations: Vec<String>,
    pub talent_authorizations: Vec<String>,
    pub singer_authorizations: Vec<String>,
}

pub fn cover_url(template: String) -> String {
    template.replace("{size}", "240")
}

/// Explicit refreshed session; encrypted wire credentials are not merged into profile.
#[derive(Serialize)]
pub struct AccountSession {
    pub user_id: u64,
    pub token: String,
    pub refresh_credential: String,
    pub expires_at_seconds: Option<u64>,
    pub nickname: String,
    pub avatar_url: String,
}
/// Initial login may omit refresh/expiry. Unknown values remain absent; never
/// manufacture a refresh token or an expiry to satisfy the refreshed-session DTO.
#[derive(Serialize)]
pub struct LoginAuthorization {
    pub user_id: u64,
    pub token: String,
    pub refresh_credential: Option<String>,
    pub expires_at_seconds: Option<u64>,
    pub nickname: String,
    pub avatar_url: String,
}
#[derive(Serialize)]
pub struct QrLoginSession {
    pub key: String,
    pub image_data_url: String,
}
#[derive(Debug, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum QrLoginState {
    Waiting,
    Scanned,
    Expired,
    Authorized,
}

/// QR authorization does not supply t1 or expiry. Refresh is a separate request;
/// never manufacture those credentials from the password/token-login contract.
#[derive(Serialize)]
pub struct QrAuthorization {
    pub user_id: u64,
    pub token: String,
    pub nickname: String,
    pub avatar_url: String,
}
#[derive(Serialize)]
pub struct QrLoginStatus {
    pub state: QrLoginState,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub authorization: Option<QrAuthorization>,
}
impl std::fmt::Debug for QrLoginStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("QrLoginStatus")
            .field("state", &self.state)
            .field("has_authorization", &self.authorization.is_some())
            .finish()
    }
}

#[derive(Serialize)]
pub struct RegisteredDevice {
    pub dfid: String,
}
/// Mutation acknowledgements do not replace canonical read models. Clients read
/// back after writes and reconcile transport/cancellation outcomes before retry.
#[derive(Debug, serde::Serialize)]
pub struct LibraryPlaylistChange {
    pub user_id: u64,
    pub cloud_list_id: u64,
    pub previous_total_revision: u64,
    pub total_revision: u64,
    pub playlist_count: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cover_url: Option<String>,
}
#[derive(Debug, serde::Serialize)]
pub struct TrackWriteOutcome {
    pub position: Option<u32>,
    pub file_id: u64,
    pub song_id: Option<u64>,
    pub resource_code: i64,
}
#[derive(Debug, serde::Serialize)]
pub struct CloudTrackChange {
    pub user_id: u64,
    pub cloud_list_id: u64,
    pub previous_revision: u64,
    pub revision: u64,
    pub song_count: Option<u64>,
    pub outcomes: Option<Vec<TrackWriteOutcome>>,
    pub replaced_file_ids: Option<Vec<Option<u64>>>,
}

/// Explicit social identity: artist-only records never invent a user ID.
#[derive(Debug, Serialize)]
pub struct SocialUser {
    pub key: String,
    pub user_id: u64,
    pub artist_id: Option<u64>,
    pub nickname: String,
    pub avatar_url: String,
    pub description: String,
    pub grade: Option<u32>,
    pub following: Option<bool>,
    pub mutual: Option<bool>,
    pub visited_at_seconds: Option<u64>,
    pub identity: ProfileIdentity,
    pub membership: SocialMembership,
    pub identity_facts: SocialIdentityFacts,
}
/// Provider facts stay nullable; badge booleans above are presentation only.
#[derive(Debug, Default, Serialize)]
pub struct SocialIdentityFacts {
    pub talent_bits: Option<u32>,
    pub star_code: Option<i32>,
    pub singer_status: Option<u32>,
    pub vip_type: Option<u32>,
    pub user_type: Option<u32>,
    pub friend_code: Option<u32>,
    /// Raw follow-list iden_type. Favorites identifies singers using
    /// singerid with iden_type == 1.
    pub identity_type: Option<u32>,
}
#[derive(Debug, Default, Serialize)]
pub struct SocialMembership {
    pub super_active: bool,
    pub deluxe_active: bool,
}
#[derive(Debug, Serialize)]
pub struct SocialPage {
    pub users: Vec<SocialUser>,
    pub total: Option<u64>,
    pub next_page: Option<u32>,
    pub visitor_count: Option<u64>,
    pub list_revision: Option<u64>,
    /// Only friends currently lacks a nonempty capture. Keep those entries
    /// separately until their business model is established; never fake rows.
    pub unmapped_entries: Option<serde_json::Value>,
}
#[derive(Debug, Serialize)]
pub struct LoginDevice {
    pub id: String,
    /// Target fingerprint for explicit device logout; never displayed by QML.
    pub transport_fingerprint: String,
    pub model: String,
    pub platform: String,
    pub location: String,
    pub login_type: Option<i64>,
    pub active_at_seconds: Option<u64>,
    pub new_at_seconds: Option<u64>,
    pub app_id: Option<u32>,
    pub client_version: Option<u32>,
    pub current: bool,
}

pub mod discovery;

pub mod listen_together;
