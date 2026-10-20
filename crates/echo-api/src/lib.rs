//! Native protocol library with endpoint-specific field mapping.
//! Live integration coverage is separate from decoding and unit-test coverage.
pub mod domain;
pub mod error;
pub mod module;
pub mod util;

use serde::{Deserialize, Serialize};

use util::request::{ApiClient, Identity};

/// Decode the bounded, untrusted request bytes accepted by the C ABI. Keeping
/// this safe function shared lets fuzzers exercise the production ingress
/// without inventing invalid native pointers or constructing an audio runtime.
pub fn decode_ffi_request(input: &[u8]) -> Option<Request> {
    if input.is_empty() || input.len() > 1024 * 1024 {
        return None;
    }
    serde_json::from_slice(input).ok()
}

#[derive(Deserialize)]
#[serde(tag = "operation", rename_all = "snake_case", deny_unknown_fields)]
pub enum Request {
    AudioMatch {
        query: module::audio_match::Query,
        identity: Identity,
    },
    TopSong {
        identity: Identity,
    },
    TopAlbum {
        identity: Identity,
    },
    PlaylistTags {
        identity: Identity,
    },
    ArtistLists {
        query: module::artist_lists::Query,
        identity: Identity,
    },
    KrmAudio {
        query: module::krm_audio::Query,
        identity: Identity,
    },
    Capabilities,
    TopPlaylist {
        query: module::top_playlist::Query,
        identity: Identity,
    },
    TopIp {
        identity: Identity,
    },
    EverydayStyleRecommend {
        query: module::everyday_style_recommend::Query,
        identity: Identity,
    },
    EverydayRecommend {
        identity: Identity,
    },
    AiRecommendSong {
        identity: Identity,
    },
    ListenTogetherRoom {
        query: module::listen_together_room::Query,
        identity: Identity,
    },
    ListenTogetherMusic {
        query: module::listen_together_music::Query,
        identity: Identity,
    },
    ListenTogetherChat {
        query: module::listen_together_chat::Query,
        identity: Identity,
    },
    PersonalFm {
        query: module::personal_fm::Query,
        identity: Identity,
    },
    HomeDiscover {
        query: module::home_discover::Query,
        identity: Identity,
    },
    RankTop {
        identity: Identity,
    },
    RankList {
        identity: Identity,
    },
    RankAudio {
        query: module::rank_audio::Query,
        identity: Identity,
    },
    ImportPlaylist {
        query: module::import_playlist::Query,
        identity: Identity,
    },
    PlaylistSort {
        query: module::playlist_sort::Query,
        identity: Identity,
    },
    PlaylistPicUpload {
        query: module::playlist_pic_upload::Query,
    },
    PlaylistAdd {
        query: module::playlist_add::Query,
        identity: Identity,
    },
    PlaylistDel {
        query: module::playlist_del::Query,
        identity: Identity,
    },
    PlaylistTracksAdd {
        query: module::playlist_tracks_add::Query,
        identity: Identity,
    },
    PlaylistTracksDel {
        query: module::playlist_tracks_del::Query,
        identity: Identity,
    },
    PlaylistTracksSort {
        query: module::playlist_tracks_sort::Query,
        identity: Identity,
    },
    PlaylistUpdate {
        query: module::playlist_update::Query,
        identity: Identity,
    },

    UserPlaylist {
        query: module::user_playlist::Query,
        identity: Identity,
    },
    UserCloud {
        query: module::user_cloud::Query,
        identity: Identity,
    },
    UserCloudUrl {
        query: module::user_cloud_url::Query,
        identity: Identity,
    },
    UserCloudMatch {
        query: module::user_cloud_match::Query,
        identity: Identity,
    },
    UserCloudUpload {
        query: module::user_cloud_upload::Query,
        identity: Identity,
    },
    UserCloudDel {
        query: module::user_cloud_del::Query,
        identity: Identity,
    },
    UserVideoCollect {
        query: module::user_video_collect::Query,
        identity: Identity,
    },
    UserPurchasedSongs {
        query: module::user_purchased_songs::Query,
        identity: Identity,
    },
    UserPurchasedAlbums {
        query: module::user_purchased_albums::Query,
        identity: Identity,
    },
    PlaylistDetail {
        query: module::playlist_detail::Query,
        identity: Identity,
    },
    PlaylistTrackAll {
        query: module::playlist_track_all::Query,
        identity: Identity,
    },
    PlaylistTrackAllNew {
        query: module::playlist_track_all_new::Query,
        identity: Identity,
    },
    LoginToken {
        identity: Identity,
    },
    RegisterDev {
        identity: Identity,
    },
    LoginQrKey {
        identity: Identity,
    },
    LoginQrCheck {
        query: module::login_qr_check::Query,
        identity: Identity,
    },
    CaptchaSent {
        query: module::captcha_sent::Query,
        identity: Identity,
    },
    LoginCellphone {
        query: module::login_cellphone::Query,
        identity: Identity,
    },
    LoginQq {
        query: module::login_qq::Query,
        identity: Identity,
    },
    LoginOpenplat {
        query: module::login_openplat::Query,
        identity: Identity,
    },
    LoginQqQrCreate,
    LoginQqQrCheck {
        query: module::login_qq_qr_check::Query,
        identity: Identity,
    },
    LoginWxCreate,
    LoginWxCheck {
        query: module::login_wx_check::Query,
    },
    Login {
        query: module::login::Query,
        identity: Identity,
    },
    GetVerifyInfo {
        query: module::get_verify_info::Query,
        identity: Identity,
    },
    Sidedt {
        query: module::verify_user_info::Query,
        identity: Identity,
    },
    SearchHot {
        identity: Identity,
    },
    SearchDefault {
        identity: Identity,
    },
    Search {
        query: module::search::Query,
        identity: Identity,
    },
    ArtistFollow {
        id: u64,
        identity: Identity,
    },
    ArtistUnfollow {
        id: u64,
        identity: Identity,
    },
    UserFollowAdd {
        id: u64,
        identity: Identity,
    },
    UserFollowDel {
        id: u64,
        identity: Identity,
    },
    UserFollowMessage {
        query: module::user_follow_message::Query,
        identity: Identity,
    },
    UserFollowChat {
        query: module::user_follow_chat::Query,
        identity: Identity,
    },
    UserFollow {
        identity: Identity,
    },
    UserFans {
        identity: Identity,
    },
    UserFriends {
        identity: Identity,
    },
    UserVisitors {
        identity: Identity,
    },
    LoginDevice {
        identity: Identity,
    },
    LoginDeviceKick {
        query: module::login_device_kick::Query,
        identity: Identity,
    },
    BlacklistList {
        query: module::blacklist_list::Query,
        identity: Identity,
    },
    Blacklist {
        query: module::blacklist::Query,
        identity: Identity,
    },
    UserPreference {
        identity: Identity,
    },
    UserPreferenceUpdate {
        query: module::user_preference_update::Query,
        identity: Identity,
    },
    UserUpdate {
        query: module::user_update::Query,
        identity: Identity,
    },
    UserUpdateAvatar {
        query: module::user_update_avatar::Query,
        identity: Identity,
    },
    UserDetail {
        identity: Identity,
    },
    UserInfo {
        identity: Identity,
    },
    UserVipDetail {
        identity: Identity,
    },
    UserListenReport {
        query: module::user_listen_report::Query,
        identity: Identity,
    },
    UserGradeProgress {
        query: module::user_grade_info::Progress,
        identity: Identity,
    },
    UserGradeInfo {
        query: module::user_grade_info::Query,
        identity: Identity,
    },
    SearchSuggest {
        query: module::search_suggest::Query,
        identity: Identity,
    },
    SongClimax {
        query: module::song_climax::Query,
        identity: Identity,
    },
    AlbumDycover {
        query: module::album_dycover::Query,
        identity: Identity,
    },
    SongRanking {
        query: module::song_ranking::Query,
        identity: Identity,
    },
    SongRankingFilter {
        query: module::song_ranking_filter::Query,
        identity: Identity,
    },
    FavoriteCount {
        query: module::favorite_count::Query,
        identity: Identity,
    },
    CommentCount {
        query: module::comment_count::Query,
        identity: Identity,
    },
    CommentMusic {
        query: module::comment_music::Query,
        identity: Identity,
    },
    CommentAlbum {
        query: module::comment_album::Query,
        identity: Identity,
    },
    CommentPlaylist {
        query: module::comment_playlist::Query,
        identity: Identity,
    },
    CommentMusicClassify {
        query: module::comment_music_classify::Query,
        identity: Identity,
    },
    CommentMusicHotword {
        query: module::comment_music_hotword::Query,
        identity: Identity,
    },
    CommentFloor {
        query: module::comment_floor::Query,
        identity: Identity,
    },
    KmrAudioMv {
        query: module::kmr_audio_mv::Query,
        identity: Identity,
    },
    VideoDetail {
        query: module::video_detail::Query,
        identity: Identity,
    },
    VideoPrivilege {
        query: module::video_privilege::Query,
        identity: Identity,
    },
    VideoUrl {
        query: module::video_url::Query,
        identity: Identity,
    },
    CommentMusicSend {
        query: module::comment_music_send::Query,
        identity: Identity,
    },
    CommentAlbumSend {
        query: module::comment_album_send::Query,
        identity: Identity,
    },
    CommentPlaylistSend {
        query: module::comment_playlist_send::Query,
        identity: Identity,
    },
    CommentFloorSend {
        query: module::comment_floor_send::Query,
        identity: Identity,
    },
    CommentMusicDel {
        query: module::comment_music_del::Query,
        identity: Identity,
    },
    MvCollect {
        query: module::mv_collect::Query,
        identity: Identity,
    },
    MvCollectDel {
        query: module::mv_collect_del::Query,
        identity: Identity,
    },
    UserBatchUnionVipinfo {
        query: module::user_batch_union_vipinfo::Query,
        identity: Identity,
    },
    SongBarrage {
        query: module::song_barrage::Query,
        identity: Identity,
    },
    SongBarrageSend {
        query: module::song_barrage_send::Query,
        identity: Identity,
    },
    VideoBarrage {
        query: module::video_barrage::Query,
        identity: Identity,
    },
    VideoBarrageSend {
        query: module::video_barrage_send::Query,
        identity: Identity,
    },
    AlbumDetail {
        id: u64,
        identity: Identity,
    },
    AlbumSongs {
        query: module::album_songs::Query,
        identity: Identity,
    },
    ArtistDetail {
        id: u64,
        identity: Identity,
    },
    ArtistSongs {
        query: module::artist_audios_new::Query,
        identity: Identity,
    },
    ArtistAlbums {
        query: module::artist_albums::Query,
        identity: Identity,
    },
    UserVerify {
        identity: Identity,
    },
    SearchLyric {
        query: module::search_lyric::Query,
        identity: Identity,
    },
    Lyric {
        query: module::lyric::Query,
        identity: Identity,
    },
    EffectsArtist {
        query: module::effects_artist::Query,
        identity: Identity,
    },
    EffectsBrand {
        query: module::effects_brand::Query,
        identity: Identity,
    },
    EffectsBrandDetail {
        query: module::effects_brand_detail::Query,
        identity: Identity,
    },
    EffectsMatch {
        identity: Identity,
    },
    GetModel {
        query: module::get_model::Query,
        identity: Identity,
    },
    SongUrlAuthMerge {
        query: module::song_url_auth_merge::Query,
        identity: Identity,
    },
    PrivilegeLite {
        query: module::privilege_lite::Query,
        identity: Identity,
    },
}

#[derive(Debug, Serialize)]
pub struct Capabilities {
    pub abi_version: u32,
    pub api_routes: Vec<&'static str>,
}

pub async fn execute(request: Request, client: &ApiClient) -> Result<Vec<u8>, error::ApiError> {
    match request {
        Request::CaptchaSent { query, identity } => {
            module::captcha_sent::execute(client, &identity, query).await
        }
        Request::LoginCellphone { query, identity } => {
            module::login_cellphone::execute(client, &identity, query).await
        }
        Request::LoginQq { query, identity } => {
            module::login_qq::execute(client, &identity, query).await
        }
        Request::LoginOpenplat { query, identity } => {
            module::login_openplat::execute(client, &identity, query).await
        }
        Request::LoginQqQrCreate => module::login_qq_qr_create::execute(client).await,
        Request::LoginQqQrCheck { query, identity: _ } => {
            module::login_qq_qr_check::execute(client, query).await
        }
        Request::LoginWxCreate => module::login_wx_create::execute(client).await,
        Request::LoginWxCheck { query } => module::login_wx_check::execute(client, query).await,
        Request::AudioMatch { query, identity } => {
            module::audio_match::execute(client, query, &identity).await
        }
        Request::Capabilities => util::request::encode(&Capabilities {
            abi_version: 1,
            api_routes: vec![
                "/audio/match",
                "/top/playlist",
                "/top/ip",
                "/everyday/style/recommend",
                "/everyday/recommend",
                "/rank/top",
                "/rank/list",
                "/rank/audio",
                "/login/qq",
                "/login/openplat",
                "/login/qq/qr/create",
                "/login/qq/qr/check",
                "/login/wx/create",
                "/login/wx/check",
                "/captcha/sent",
                "/login/cellphone",
                "/search",
                "/user/playlist",
                "/playlist/sort",
                "/import/playlist",
                "/playlist/pic/upload",
                "/playlist/add",
                "/playlist/del",
                "/playlist/tracks/add",
                "/playlist/tracks/del",
                "/playlist/tracks/sort",
                "/playlist/update",
                "/playlist/detail",
                "/playlist/track/all",
                "/playlist/track/all/new",
                "/login/token",
                "/register/dev",
                "/login/qr/key",
                "/login/qr/check",
                "/login",
                "/get/verify/info",
                "/sidedt",
                "/search/hot",
                "/search/default",
                "/user/detail",
                "/user/follow",
                "/user/fans",
                "/user/friends",
                "/user/visitors",
                "/login/device",
                "/login/device/kick",
                "/user/update",
                "/user/preference",
                "/user/preference/update",
                "/user/info",
                "/user/vip/detail",
                "/user/grade/info",
                "/user/listen/report",
                "/search/suggest",
                "/album/detail",
                "/album/songs",
                "/user/verify",
                "/song/url/auth/merge",
                "/privilege/lite",
                "/ai/recommend/song",
                "/home/discover",
                "/user/cloud",
                "/user/cloud/del",
                "/user/video/collect",
                "/user/purchased/songs",
                "/user/purchased/albums",
            ],
        }),
        Request::ImportPlaylist { query, identity } => {
            module::import_playlist::execute(client, &identity, query).await
        }
        Request::PlaylistSort { query, identity } => {
            module::playlist_sort::execute(client, &identity, query).await
        }
        Request::PlaylistPicUpload { query } => {
            module::playlist_pic_upload::execute(client, query).await
        }
        Request::PlaylistAdd { query, identity } => {
            module::playlist_add::execute(client, &identity, query).await
        }
        Request::PlaylistDel { query, identity } => {
            module::playlist_del::execute(client, &identity, query).await
        }
        Request::PlaylistTracksAdd { query, identity } => {
            module::playlist_tracks_add::execute(client, &identity, query).await
        }
        Request::PlaylistTracksDel { query, identity } => {
            module::playlist_tracks_del::execute(client, &identity, query).await
        }
        Request::PlaylistTracksSort { query, identity } => {
            module::playlist_tracks_sort::execute(client, &identity, query).await
        }
        Request::PlaylistUpdate { query, identity } => {
            module::playlist_update::execute(client, &identity, query).await
        }
        Request::SearchHot { identity } => module::search_hot::execute(client, &identity).await,
        Request::UserPlaylist { query, identity } => {
            module::user_playlist::execute(client, &identity, query).await
        }
        Request::PlaylistDetail { query, identity } => {
            module::playlist_detail::execute(client, &identity, query).await
        }
        Request::PlaylistTrackAll { query, identity } => {
            module::playlist_track_all::execute(client, &identity, query).await
        }
        Request::KrmAudio { query, identity } => {
            module::krm_audio::execute(client, &identity, query).await
        }
        Request::TopSong { identity } => module::top_song::execute(client, &identity).await,
        Request::TopAlbum { identity } => module::top_album::execute(client, &identity).await,
        Request::PlaylistTags { identity } => {
            module::playlist_tags::execute(client, &identity).await
        }
        Request::ArtistLists { query, identity } => {
            module::artist_lists::execute(client, &identity, query).await
        }
        Request::UserCloud { query, identity } => {
            module::user_cloud::execute(client, &identity, query).await
        }
        Request::UserCloudUrl { query, identity } => {
            module::user_cloud_url::execute(client, &identity, query).await
        }
        Request::UserCloudMatch { query, identity } => {
            module::user_cloud_match::execute(client, &identity, query).await
        }
        Request::UserCloudUpload { query, identity } => {
            module::user_cloud_upload::execute(client, &identity, query).await
        }
        Request::UserCloudDel { query, identity } => {
            module::user_cloud_del::execute(client, &identity, query).await
        }
        Request::UserVideoCollect { query, identity } => {
            module::user_video_collect::execute(client, &identity, query).await
        }
        Request::UserPurchasedSongs { query, identity } => {
            module::user_purchased_songs::execute(client, &identity, query).await
        }
        Request::UserPurchasedAlbums { query, identity } => {
            module::user_purchased_albums::execute(client, &identity, query).await
        }
        Request::PlaylistTrackAllNew { query, identity } => {
            module::playlist_track_all_new::execute(client, &identity, query).await
        }
        Request::LoginToken { identity } => module::login_token::execute(client, &identity).await,
        Request::RegisterDev { identity } => module::register_dev::execute(client, &identity).await,
        Request::LoginQrKey { identity } => module::login_qr_key::execute(client, &identity).await,
        Request::LoginQrCheck { query, identity } => {
            module::login_qr_check::execute(client, &identity, query).await
        }
        Request::Login { query, identity } => {
            module::login::execute(client, &identity, query).await
        }
        Request::GetVerifyInfo { query, identity } => {
            module::get_verify_info::execute(client, &identity, query).await
        }
        Request::Sidedt { query, identity } => {
            module::sidedt::execute(client, &identity, query).await
        }
        Request::SearchDefault { identity } => {
            module::search_default::execute(client, &identity).await
        }
        Request::Search { query, identity } => {
            module::search::execute(client, &identity, query).await
        }
        Request::ArtistFollow { id, identity } => {
            module::artist_follow::execute(client, &identity, id).await
        }
        Request::ArtistUnfollow { id, identity } => {
            module::artist_unfollow::execute(client, &identity, id).await
        }
        Request::UserFollowAdd { id, identity } => {
            module::user_follow_add::execute(client, &identity, id).await
        }
        Request::UserFollowDel { id, identity } => {
            module::user_follow_del::execute(client, &identity, id).await
        }
        Request::UserFollowMessage { query, identity } => {
            module::user_follow_message::execute(client, &identity, query).await
        }
        Request::UserFollowChat { query, identity } => {
            module::user_follow_chat::execute(client, &identity, query).await
        }
        Request::UserFollow { identity } => module::user_follow::execute(client, &identity).await,
        Request::UserFans { identity } => module::user_fans::execute(client, &identity).await,
        Request::UserFriends { identity } => module::user_friends::execute(client, &identity).await,
        Request::UserVisitors { identity } => {
            module::user_visitors::execute(client, &identity).await
        }
        Request::LoginDevice { identity } => module::login_device::execute(client, &identity).await,
        Request::LoginDeviceKick { query, identity } => {
            module::login_device_kick::execute(client, &identity, query).await
        }
        Request::BlacklistList { query, identity } => {
            module::blacklist_list::execute(client, &identity, query).await
        }
        Request::Blacklist { query, identity } => {
            module::blacklist::execute(client, &identity, query).await
        }
        Request::UserPreference { identity } => {
            module::user_preference::execute(client, &identity).await
        }
        Request::UserPreferenceUpdate { query, identity } => {
            module::user_preference_update::execute(client, &identity, query).await
        }
        Request::UserUpdate { query, identity } => {
            module::user_update::execute(client, &identity, query).await
        }
        Request::UserUpdateAvatar { query, identity } => {
            module::user_update_avatar::execute(client, &identity, query).await
        }
        Request::UserDetail { identity } => module::user_detail::execute(client, &identity).await,
        Request::UserInfo { identity } => module::user_info::execute(client, &identity).await,
        Request::UserVipDetail { identity } => {
            module::user_vip_detail::execute(client, &identity).await
        }
        Request::UserListenReport { query, identity } => {
            module::user_listen_report::execute(client, &identity, query).await
        }
        Request::UserGradeProgress { query, identity } => {
            module::user_grade_info::report(client, &identity, query).await
        }
        Request::UserGradeInfo { query, identity } => {
            module::user_grade_info::execute(client, &identity, query).await
        }
        Request::SearchSuggest { query, identity } => {
            module::search_suggest::execute(client, &identity, query).await
        }
        Request::SongRanking { query, identity } => {
            module::song_ranking::execute(client, &identity, query).await
        }
        Request::SongRankingFilter { query, identity } => {
            module::song_ranking_filter::execute(client, &identity, query).await
        }
        Request::FavoriteCount { query, identity } => {
            module::favorite_count::execute(client, &identity, query).await
        }
        Request::CommentCount { query, identity } => {
            module::comment_count::execute(client, &identity, query).await
        }
        Request::CommentMusic { query, identity } => {
            module::comment_music::execute(client, &identity, query).await
        }
        Request::CommentAlbum { query, identity } => {
            module::comment_album::execute(client, &identity, query).await
        }
        Request::CommentPlaylist { query, identity } => {
            module::comment_playlist::execute(client, &identity, query).await
        }
        Request::CommentMusicClassify { query, identity } => {
            module::comment_music_classify::execute(client, &identity, query).await
        }
        Request::CommentMusicHotword { query, identity } => {
            module::comment_music_hotword::execute(client, &identity, query).await
        }
        Request::CommentFloor { query, identity } => {
            module::comment_floor::execute(client, &identity, query).await
        }
        Request::KmrAudioMv { query, identity } => {
            module::kmr_audio_mv::execute(client, &identity, query).await
        }
        Request::VideoDetail { query, identity } => {
            module::video_detail::execute(client, &identity, query).await
        }
        Request::VideoPrivilege { query, identity } => {
            module::video_privilege::execute(client, &identity, query).await
        }
        Request::VideoUrl { query, identity } => {
            module::video_url::execute(client, &identity, query).await
        }
        Request::CommentMusicSend { query, identity } => {
            module::comment_music_send::execute(client, &identity, query).await
        }
        Request::CommentAlbumSend { query, identity } => {
            module::comment_album_send::execute(client, &identity, query).await
        }
        Request::CommentPlaylistSend { query, identity } => {
            module::comment_playlist_send::execute(client, &identity, query).await
        }
        Request::CommentFloorSend { query, identity } => {
            module::comment_floor_send::execute(client, &identity, query).await
        }
        Request::CommentMusicDel { query, identity } => {
            module::comment_music_del::execute(client, &identity, query).await
        }
        Request::MvCollect { query, identity } => {
            module::mv_collect::execute(client, &identity, query).await
        }
        Request::MvCollectDel { query, identity } => {
            module::mv_collect_del::execute(client, &identity, query).await
        }
        Request::UserBatchUnionVipinfo { query, identity } => {
            module::user_batch_union_vipinfo::execute(client, &identity, query).await
        }
        Request::SongBarrage { query, identity } => {
            module::song_barrage::execute(client, &identity, query).await
        }
        Request::SongBarrageSend { query, identity } => {
            module::song_barrage_send::execute(client, &identity, query).await
        }
        Request::VideoBarrage { query, identity } => {
            module::video_barrage::execute(client, &identity, query).await
        }
        Request::VideoBarrageSend { query, identity } => {
            module::video_barrage_send::execute(client, &identity, query).await
        }
        Request::AlbumDetail { id, identity } => {
            module::album_detail::execute(client, &identity, id).await
        }
        Request::SongClimax { query, identity } => {
            module::song_climax::execute(client, query, &identity).await
        }
        Request::AlbumDycover { query, identity: _ } => {
            module::album_dycover::execute(client, query).await
        }
        Request::TopPlaylist { query, identity } => {
            module::top_playlist::execute(client, query, &identity).await
        }
        Request::TopIp { identity } => module::top_ip::execute(client, &identity).await,
        Request::EverydayStyleRecommend { query, identity } => {
            module::everyday_style_recommend::execute(client, query, &identity).await
        }
        Request::EverydayRecommend { identity } => {
            module::everyday_recommend::execute(client, &identity).await
        }
        Request::AiRecommendSong { identity } => {
            module::ai_recommend_song::execute(client, &identity).await
        }
        Request::ListenTogetherRoom { query, identity } => {
            module::listen_together_room::execute(client, &identity, query).await
        }
        Request::ListenTogetherMusic { query, identity } => {
            module::listen_together_music::execute(client, &identity, query).await
        }
        Request::ListenTogetherChat { query, identity } => {
            module::listen_together_chat::execute(client, &identity, query).await
        }
        Request::PersonalFm { query, identity } => {
            module::personal_fm::execute(client, &identity, query).await
        }
        Request::HomeDiscover { query, identity } => {
            module::home_discover::execute(client, &identity, query).await
        }
        Request::RankTop { identity } => module::rank_top::execute(client, &identity).await,
        Request::RankList { identity } => module::rank_list::execute(client, &identity).await,
        Request::RankAudio { query, identity } => {
            module::rank_audio::execute(client, query, &identity).await
        }
        Request::AlbumSongs { query, identity } => {
            module::album_songs::execute(client, &identity, query).await
        }
        Request::ArtistDetail { id, identity } => {
            module::artist_detail::execute(client, &identity, id).await
        }
        Request::ArtistSongs { query, identity } => {
            module::artist_audios_new::execute(client, &identity, query).await
        }
        Request::ArtistAlbums { query, identity } => {
            module::artist_albums::execute(client, &identity, query).await
        }
        Request::UserVerify { identity } => module::user_verify::execute(client, &identity).await,
        Request::SearchLyric { query, identity } => {
            module::search_lyric::execute(client, &identity, query).await
        }
        Request::Lyric { query, identity } => {
            module::lyric::execute(client, &identity, query).await
        }
        Request::EffectsArtist { query, identity } => {
            module::effects_artist::execute(client, &identity, query).await
        }
        Request::EffectsBrand { query, identity } => {
            module::effects_brand::execute(client, &identity, query).await
        }
        Request::EffectsBrandDetail { query, identity } => {
            module::effects_brand_detail::execute(client, &identity, query).await
        }
        Request::EffectsMatch { identity } => {
            module::effects_match::execute(client, &identity).await
        }
        Request::GetModel { query, identity } => {
            module::get_model::execute(client, &identity, query).await
        }
        Request::SongUrlAuthMerge { query, identity } => {
            module::song_url_auth_merge::execute(client, &identity, query).await
        }
        Request::PrivilegeLite { query, identity } => {
            module::privilege_lite::execute(client, &identity, query).await
        }
    }
}
