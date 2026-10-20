//! /search has six independent item models; fields do not fall back across kinds.
use crate::{
    domain::{discovery::CatalogSongFacts, *},
    error::ApiError,
    util::{
        catalog_rights::WireTrans,
        request::{self, ApiClient, Identity},
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;
#[derive(Clone, Copy, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SearchKind {
    Song,
    Album,
    Author,
    Special,
    Lyric,
    Mv,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub keyword: String,
    pub kind: SearchKind,
    pub page: u32,
    pub page_size: u32,
}
#[derive(Deserialize)]
struct Envelope {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    data: serde_json::Value,
}
#[derive(Deserialize)]
#[serde(bound(deserialize = "T: serde::de::DeserializeOwned"))]
struct WirePage<T> {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    page: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pagesize: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::list")]
    lists: Vec<T>,
}
impl<T> WirePage<T> {
    fn map<U>(
        self,
        mut convert: impl FnMut(T) -> Result<U, ApiError>,
    ) -> Result<Page<U>, ApiError> {
        Ok(Page {
            page: self.page,
            page_size: self.pagesize,
            total: self.total,
            items: self
                .lists
                .into_iter()
                .map(&mut convert)
                .collect::<Result<_, _>>()?,
        })
    }
}
fn page<T: serde::de::DeserializeOwned>(bytes: &[u8]) -> Result<WirePage<T>, ApiError> {
    let w: Envelope = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())
}
#[derive(Deserialize)]
struct Singer {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
}
fn artists(s: Vec<Singer>) -> Vec<ArtistRef> {
    s.into_iter()
        .map(|s| ArtistRef {
            id: s.id,
            name: s.name,
        })
        .collect()
}
fn milliseconds(s: Option<u64>) -> Option<u64> {
    s.and_then(|v| v.checked_mul(1000))
}
#[derive(Default, Deserialize)]
#[serde(rename_all = "PascalCase", default)]
struct SearchQuality {
    hash: String,
    file_size: Option<u64>,
    bit_rate: Option<u64>,
    duration: Option<u64>,
    ext_name: Option<String>,
    privilege: Option<u32>,
    pay_type: Option<u32>,
    fail_process: Option<u32>,
}
#[allow(clippy::too_many_arguments)]
fn push_quality(
    resources: &mut Vec<crate::domain::discovery::AudioResource>,
    quality: &str,
    hash: String,
    size: Option<u64>,
    duration: Option<u64>,
    bitrate: Option<u64>,
    extension: Option<String>,
    privilege: Option<u32>,
    payment: Option<u32>,
    failure: Option<u32>,
) {
    let hash = (!hash.is_empty()).then_some(hash);
    if hash.is_none()
        && size.is_none()
        && duration.is_none()
        && bitrate.is_none()
        && extension.is_none()
    {
        return;
    }
    if resources
        .iter()
        .any(|resource| resource.quality == quality && resource.hash == hash)
    {
        return;
    }
    resources.push(crate::domain::discovery::AudioResource {
        quality: quality.into(),
        hash,
        size_bytes: size,
        duration_ms: milliseconds(duration),
        bit_rate_bps: bitrate.and_then(|value| value.checked_mul(1000)),
        extension,
        privilege,
        payment_type: payment,
        failure_code: failure,
        download_status: None,
    });
}
/// Song and lyric search expose the same rights, payment and quality facts but
/// spell the quality entries differently (flat fields vs nested objects).
#[derive(Default, Deserialize)]
struct SongFacts {
    #[serde(
        rename = "AlbumPrivilege",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    album_privilege: Option<u32>,
    #[serde(
        rename = "PayType",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    pay_type: Option<u32>,
    #[serde(
        rename = "OldCpy",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    old_cpy: Option<u32>,
    #[serde(
        rename = "FailProcess",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    fail_process: Option<u32>,
    #[serde(
        rename = "IsOriginal",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    original: Option<u32>,
    #[serde(
        rename = "HQFileHash",
        default,
        deserialize_with = "crate::util::response::text"
    )]
    hq_file_hash: String,
    #[serde(
        rename = "HQFileSize",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    hq_file_size: Option<u64>,
    #[serde(
        rename = "HQBitrate",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    hq_bitrate: Option<u64>,
    #[serde(
        rename = "HQDuration",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    hq_duration: Option<u64>,
    #[serde(
        rename = "HQExtName",
        default,
        deserialize_with = "crate::util::response::text"
    )]
    hq_ext_name: String,
    #[serde(
        rename = "HQPrivilege",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    hq_privilege: Option<u32>,
    #[serde(
        rename = "HQPayType",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    hq_pay_type: Option<u32>,
    #[serde(
        rename = "HQFailProcess",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    hq_fail_process: Option<u32>,
    #[serde(
        rename = "SQFileHash",
        default,
        deserialize_with = "crate::util::response::text"
    )]
    sq_file_hash: String,
    #[serde(
        rename = "SQFileSize",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    sq_file_size: Option<u64>,
    #[serde(
        rename = "SQBitrate",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    sq_bitrate: Option<u64>,
    #[serde(
        rename = "SQDuration",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    sq_duration: Option<u64>,
    #[serde(
        rename = "SQExtName",
        default,
        deserialize_with = "crate::util::response::text"
    )]
    sq_ext_name: String,
    #[serde(
        rename = "SQPrivilege",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    sq_privilege: Option<u32>,
    #[serde(
        rename = "SQPayType",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    sq_pay_type: Option<u32>,
    #[serde(
        rename = "SQFailProcess",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    sq_fail_process: Option<u32>,
    #[serde(
        rename = "ResFileHash",
        default,
        deserialize_with = "crate::util::response::text"
    )]
    res_file_hash: String,
    #[serde(
        rename = "ResFileSize",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    res_file_size: Option<u64>,
    #[serde(
        rename = "ResBitrate",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    res_bitrate: Option<u64>,
    #[serde(
        rename = "ResDuration",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    res_duration: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::object")]
    trans_param: WireTrans,
    #[serde(
        rename = "HQ",
        default,
        deserialize_with = "crate::util::response::object"
    )]
    hq: SearchQuality,
    #[serde(
        rename = "SQ",
        default,
        deserialize_with = "crate::util::response::object"
    )]
    sq: SearchQuality,
    #[serde(
        rename = "Res",
        default,
        deserialize_with = "crate::util::response::object"
    )]
    res: SearchQuality,
}
impl SongFacts {
    fn privilege(&self, fallback: Option<u32>) -> Option<u32> {
        self.album_privilege.or(fallback)
    }
    fn into_metadata(self) -> CatalogSongFacts {
        let (rights, mut resources) = match self.trans_param.convert() {
            Ok((rights, resources)) => (Some(rights), resources),
            Err(_) => (None, Vec::new()),
        };
        push_quality(
            &mut resources,
            "320",
            self.hq_file_hash,
            self.hq_file_size,
            self.hq_duration,
            self.hq_bitrate,
            Some(self.hq_ext_name).filter(|value| !value.is_empty()),
            self.hq_privilege,
            self.hq_pay_type,
            self.hq_fail_process,
        );
        push_quality(
            &mut resources,
            "flac",
            self.sq_file_hash,
            self.sq_file_size,
            self.sq_duration,
            self.sq_bitrate,
            Some(self.sq_ext_name).filter(|value| !value.is_empty()),
            self.sq_privilege,
            self.sq_pay_type,
            self.sq_fail_process,
        );
        push_quality(
            &mut resources,
            "high",
            self.res_file_hash,
            self.res_file_size,
            self.res_duration,
            self.res_bitrate,
            None,
            None,
            None,
            None,
        );
        for (quality, entry) in [("320", self.hq), ("flac", self.sq), ("high", self.res)] {
            push_quality(
                &mut resources,
                quality,
                entry.hash,
                entry.file_size,
                entry.duration,
                entry.bit_rate,
                entry.ext_name,
                entry.privilege,
                entry.pay_type,
                entry.fail_process,
            );
        }
        CatalogSongFacts {
            payment_type: self.pay_type,
            failure_code: self.fail_process,
            old_copyright_code: self.old_cpy,
            originality_code: self.original,
            rights,
            resources,
        }
    }
}
#[derive(Deserialize)]
struct Song {
    #[serde(
        rename = "Audioid",
        default,
        deserialize_with = "crate::util::response::optional"
    )]
    audio_id: Option<u64>,
    #[serde(rename = "MixSongID")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    id: String,
    #[serde(rename = "FileHash")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(rename = "SongName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    title: String,
    #[serde(rename = "Singers")]
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singers: Vec<Singer>,
    #[serde(rename = "AlbumID")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(rename = "AlbumName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_title: String,
    #[serde(rename = "Duration")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(rename = "Image")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    image: String,
    #[serde(rename = "Privilege")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(flatten)]
    facts: SongFacts,
}
#[derive(Deserialize)]
struct Album {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    albumid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    albumname: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singers: Vec<Singer>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    img: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songcount: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    publish_time: String,
}
#[derive(Deserialize)]
struct Author {
    #[serde(rename = "AuthorId")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(rename = "AuthorName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(rename = "Avatar")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    avatar: String,
    #[serde(rename = "AlbumCount")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    albums: Option<u64>,
    #[serde(rename = "AudioCount")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songs: Option<u64>,
    #[serde(rename = "FansNum")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fans: Option<u64>,
}
#[derive(Deserialize)]
struct Special {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    specialid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    specialname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    gid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    suid: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    img: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    song_count: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    play_count: String,
}
#[derive(Deserialize)]
struct Lyric {
    // Unlike song search, this variant returned a JSON number for MixSongID.
    #[serde(rename = "MixSongID")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    id: String,
    #[serde(rename = "FileHash")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(rename = "SongName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    title: String,
    #[serde(rename = "Singers")]
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singers: Vec<Singer>,
    #[serde(rename = "AlbumID")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_id: String,
    #[serde(rename = "TimeLength")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(rename = "Image")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    image: String,
    #[serde(rename = "Privilege")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    privilege: Option<u32>,
    #[serde(rename = "LyricID")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    lyric_id: Option<u64>,
    #[serde(rename = "Lyric")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    excerpt: String,
    #[serde(flatten)]
    facts: SongFacts,
}
#[derive(Deserialize)]
struct Mv {
    #[serde(rename = "MvID")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(rename = "MvHash")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    hash: String,
    #[serde(rename = "MvName")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    title: String,
    #[serde(rename = "Singers")]
    #[serde(default, deserialize_with = "crate::util::response::list")]
    singers: Vec<Singer>,
    #[serde(rename = "Duration")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(rename = "Pic")]
    #[serde(default, deserialize_with = "crate::util::response::text")]
    thumbnail: String,
}
pub fn convert(kind: SearchKind, bytes: &[u8]) -> Result<SearchResults, ApiError> {
    Ok(match kind {
        SearchKind::Song => SearchResults::Songs(page::<Song>(bytes)?.map(|s| {
            let original = s.facts.original;
            let facts = s.facts;
            let privilege = facts.privilege(s.privilege);
            let metadata = facts.into_metadata();
            Ok(SearchSong {
                audio_id: s.audio_id,
                original: original.and_then(crate::util::numeric::boolean),
                original_code: original,
                song: SongSummary {
                    id: s.id,
                    hash: s.hash,
                    title: s.title,
                    artists: artists(s.singers),
                    album_id: s.album_id,
                    album_title: Some(s.album_title),
                    duration_ms: milliseconds(s.duration),
                    cover_url: cover_url(s.image),
                    privilege,
                },
                metadata,
            })
        })?),
        SearchKind::Album => SearchResults::Albums(page::<Album>(bytes)?.map(|a| {
            Ok(AlbumSummary {
                id: a.albumid,
                title: a.albumname,
                artists: artists(a.singers),
                cover_url: cover_url(a.img),
                song_count: a.songcount,
                published_at: a.publish_time,
            })
        })?),
        SearchKind::Author => SearchResults::Artists(page::<Author>(bytes)?.map(|a| {
            Ok(ArtistSummary {
                id: a.id,
                name: a.name,
                avatar_url: a.avatar,
                album_count: a.albums,
                song_count: a.songs,
                fans: a.fans,
            })
        })?),
        SearchKind::Special => SearchResults::Playlists(page::<Special>(bytes)?.map(|s| {
            Ok(PlaylistSummary {
                id: s.specialid,
                global_id: s.gid,
                title: s.specialname,
                creator_id: s.suid,
                creator_name: s.nickname,
                cover_url: s.img,
                song_count: s.song_count,
                play_count: s.play_count.parse().ok(),
            })
        })?),
        SearchKind::Lyric => SearchResults::Lyrics(page::<Lyric>(bytes)?.map(|s| {
            let facts = s.facts;
            let privilege = facts.privilege(s.privilege);
            let metadata = facts.into_metadata();
            Ok(LyricHit {
                lyric_id: s.lyric_id,
                excerpt: s.excerpt,
                song: SongSummary {
                    id: s.id.to_string(),
                    hash: s.hash,
                    title: s.title,
                    artists: artists(s.singers),
                    album_id: s.album_id,
                    album_title: None,
                    duration_ms: milliseconds(s.duration),
                    cover_url: cover_url(s.image),
                    privilege,
                },
                metadata,
            })
        })?),
        SearchKind::Mv => SearchResults::Mvs(page::<Mv>(bytes)?.map(|m| {
            Ok(MvSummary {
                id: m.id,
                hash: m.hash,
                title: m.title,
                artists: artists(m.singers),
                duration_ms: milliseconds(m.duration),
                thumbnail_file: m.thumbnail,
            })
        })?),
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    if q.keyword.trim().is_empty() || q.page == 0 || !(1..=100).contains(&q.page_size) {
        return Err(ApiError::new("input", "搜索关键词或分页参数无效"));
    }
    let mut params = BTreeMap::from([
        ("keyword".into(), q.keyword),
        ("page".into(), q.page.to_string()),
        ("pagesize".into(), q.page_size.to_string()),
        ("platform".into(), "AndroidFilter".into()),
        ("iscorrection".into(), "1".into()),
    ]);
    let path = match q.kind {
        SearchKind::Song => {
            params.extend([
                // Real anonymous sampling: omitting userid returns error 152;
                // explicit zero succeeds without injecting token/t1. This is an
                // endpoint parameter, not an exception to skip-auth policy.
                ("userid".into(), identity.userid.to_string()),
                ("clientver".into(), "201".into()),
                ("privilegefilter".into(), "0".into()),
                ("area_code".into(), "1".into()),
                ("dopicfull".into(), "1".into()),
            ]);
            "/v2/search/song"
        }
        SearchKind::Album => {
            params.extend([
                ("clientver".into(), "201".into()),
                ("category".into(), "1".into()),
                ("sorttype".into(), "0".into()),
                ("searchsong".into(), "0".into()),
            ]);
            "/v1/search/album"
        }
        other => {
            params.extend([
                ("albumhide".into(), "0".into()),
                ("nocollect".into(), "0".into()),
            ]);
            match other {
                SearchKind::Author => "/v1/search/author",
                SearchKind::Special => "/v1/search/special",
                SearchKind::Lyric => "/v1/search/lyric",
                SearchKind::Mv => "/v1/search/mv",
                _ => unreachable!(),
            }
        }
    };
    let bytes = client
        .request(
            identity,
            path,
            Some("complexsearch.kugou.com"),
            reqwest::Method::GET,
            params,
            vec![],
        )
        .await?;
    request::encode(&convert(q.kind, &bytes)?)
}
