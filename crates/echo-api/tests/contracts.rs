use echo_api::{
    domain::SearchResults,
    module::{
        album_detail, album_songs, artist_albums, artist_audios_new, artist_detail, privilege_lite,
        search::{self, SearchKind},
        search_default, search_hot, search_suggest, song_auth, song_url_auth, user_detail,
        user_verify,
    },
};
use serde_json::Value;
use std::{fs, path::PathBuf};
fn sample(path: &str) -> Vec<u8> {
    sample_response(path, 0)
}
fn sample_response(path: &str, index: usize) -> Vec<u8> {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/api");
    let record: Value = serde_json::from_slice(&fs::read(root.join(path)).unwrap()).unwrap();
    let mut payload = record["responses"][index]["payload"].clone();
    // Fixtures redact private resource hashes. Restore only their format for
    // validation, never credentials or a fabricated provider response branch.
    fn hash_format(value: &mut Value) {
        match value {
            Value::Object(map) => {
                // Older private captures removed numeric album-ID strings.
                // A format-only anonymous stand-in uses the retained public
                // album-details ID. Fresh captures preserve the actual string.
                if map.get("album_id").and_then(Value::as_str) == Some("[REDACTED]") {
                    let id = map
                        .get("albuminfo")
                        .and_then(|v| v.get("id"))
                        .and_then(Value::as_u64)
                        .unwrap_or(1);
                    map.insert("album_id".into(), id.to_string().into());
                }
                for (key, item) in map {
                    if (key == "hash" || key.ends_with("_hash"))
                        && item.as_str() == Some("[REDACTED]")
                    {
                        *item = "0123456789ABCDEF0123456789ABCDEF".into();
                    } else {
                        hash_format(item);
                    }
                }
            }
            Value::Array(items) => {
                for item in items {
                    hash_format(item);
                }
            }
            _ => (),
        }
    }
    hash_format(&mut payload);
    // Axios JSON parsing normalizes numeric exponent syntax. Replay the exact
    // original numeric lexemes captured before that parser, not fabricated data.
    if let Some(tokens) =
        record["responses"][index]["field_shapes"]["wire_ipmap_attr0_lexemes"].as_array()
    {
        let mut tokens = tokens.iter();
        if let Some(items) = payload["data"]["info"].as_array_mut() {
            for item in items {
                if let Some(map) = item["trans_param"]["ipmap"].as_object_mut() {
                    let token = tokens.next().unwrap().as_str().unwrap();
                    map.insert(
                        "attr0".into(),
                        serde_json::Value::Number(token.parse().unwrap()),
                    );
                }
            }
        }
        assert!(tokens.next().is_none());
    }
    serde_json::to_vec(&payload).unwrap()
}
#[test]
fn anonymous_search_variants_and_explicit_zero_userid_use_real_samples() {
    let rejected =
        search::convert(SearchKind::Song, &sample("search/song-device-only.json")).unwrap_err();
    assert_eq!(rejected.kind, "business");
    assert_eq!(rejected.code, Some(152));
    let SearchResults::Songs(songs) = search::convert(
        SearchKind::Song,
        &sample("search/song-anonymous-explicit-userid.json"),
    )
    .unwrap() else {
        panic!("song kind");
    };
    assert_eq!(songs.page_size, Some(30));
    assert_eq!(songs.items.len(), 30);
    for (kind, name) in [
        (SearchKind::Album, "album"),
        (SearchKind::Author, "author"),
        (SearchKind::Special, "special"),
        (SearchKind::Lyric, "lyric"),
        (SearchKind::Mv, "mv"),
    ] {
        search::convert(kind, &sample(&format!("search/{name}-device-only.json"))).unwrap();
    }
}

#[test]
fn explore_categories_albums_artists_and_new_song_facts_use_independent_samples() {
    use echo_api::module::{artist_lists, playlist_tags, top_album, top_song};
    let songs = top_song::convert(&sample("top_song/new-songs-explore-fields-20261016.json"))
        .unwrap_or_else(|e| panic!("{}", e.message));
    assert_eq!(songs.songs.len(), 30);
    for s in songs.songs {
        assert!(s.song.duration_ms > Some(1000));
        assert!(!s.metadata.catalog.source_file_name.is_empty());
        assert!(s.metadata.catalog.publication_code.is_none());
        assert!(
            s.metadata
                .catalog
                .artists
                .iter()
                .all(|a| a.publication_code.is_some())
        );
        assert!(
            s.metadata
                .catalog
                .resources
                .iter()
                .any(|r| r.quality == "flac")
        );
        assert!(
            !s.metadata
                .catalog
                .rights
                .quality_map
                .as_ref()
                .unwrap()
                .bits
                .as_ref()
                .unwrap()
                .is_empty()
        );
    }
    let tags = playlist_tags::convert(&sample(
        "playlist_tags/explore-tags-explore-fields-20261016.json",
    ))
    .unwrap();
    assert_eq!(tags.len(), 6);
    assert!(tags.iter().all(|g| !g.children.is_empty()));
    let category = echo_api::module::top_playlist::convert(&sample(
        "top_playlist/explore-first-category-explore-final-20261016.json",
    ))
    .unwrap();
    assert!(!category.is_empty());
    let albums =
        top_album::convert(&sample("top_album/new-albums-explore-fields-20261016.json")).unwrap();
    assert_eq!(albums.regions.len(), 4);
    assert!(albums.regions.values().all(|v| v.len() == 3));
    for label in ["explore-artists", "explore-filtered-artists"] {
        let artists = artist_lists::convert(&sample(&format!(
            "artist_lists/{label}-explore-fields-20261016.json"
        )))
        .unwrap_or_else(|e| panic!("{}", e.message));
        assert_eq!(artists.groups.first().unwrap().label, "热门");
        assert!(artists.groups.len() > 20);
        assert_eq!(artists.sex_filters.len(), 4);
        assert!(artists.type_filters.iter().any(|t| t.musician == Some(3)));
        assert!(
            artists
                .groups
                .iter()
                .flat_map(|g| &g.artists)
                .all(|a| a.cover_url.starts_with("http"))
        );
    }
    let mut invalid: Value =
        serde_json::from_slice(&sample("top_song/new-songs-explore-fields-20261016.json")).unwrap();
    invalid["data"][0]["album_audio_id"] = Value::String("123".into());
    assert_eq!(
        top_song::convert(&serde_json::to_vec(&invalid).unwrap())
            .unwrap()
            .songs[0]
            .song
            .id,
        "123"
    );
}
#[test]
fn library_sort_maps_revisions_without_echo_consistency_checks() {
    use echo_api::module::playlist_sort;
    for path in ["owned-swapped", "owned-same-order", "collected-same-order"] {
        let bytes = sample(&format!("playlist_sort/{path}.json"));
        let mut raw: Value = serde_json::from_slice(&bytes).unwrap();
        let ack = playlist_sort::convert(&bytes).unwrap();
        assert!(ack.total_revision > ack.previous_total_revision);
        assert_eq!(
            ack.positions[0].cloud_list_id,
            raw["data"]["info"][0]["listid"].as_u64().unwrap()
        );
        raw["data"]["info"].as_array_mut().unwrap().reverse();
        raw["data"]["info"][0]["sort"] = 999.into();
        raw["data"]["info"][0]["type"] = 42.into();
        let changed = playlist_sort::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
        assert_eq!(changed.total_revision, ack.total_revision);
        assert_eq!(changed.positions[0].position, 999);
        assert_eq!(changed.positions[0].kind_code, 42);
        assert!(changed.positions[0].kind.is_none());
    }
    assert_eq!(
        playlist_sort::convert(&sample("playlist_sort/invalid-token.json"))
            .unwrap_err()
            .kind,
        "authentication"
    );
}

#[test]
fn playlist_tracks_sort_accepts_liked_lists_beyond_one_page() {
    use echo_api::module::playlist_tracks_sort::{Position, Query, validate};
    // Liked playlists can span multiple 300-row pages; ordering accepts all positions.
    let query = Query {
        cloud_list_id: 900079,
        revision: 2,
        positions: (0..301u32)
            .map(|index| Position {
                file_id: 1_000_000 + u64::from(index),
                position: index,
            })
            .collect(),
    };
    validate(&query).expect("301 positions must be accepted");
    assert!(
        validate(&Query {
            positions: Vec::new(),
            ..query
        })
        .is_err()
    );
    let duplicated = Query {
        cloud_list_id: 900079,
        revision: 2,
        positions: vec![
            Position {
                file_id: 7,
                position: 0,
            },
            Position {
                file_id: 7,
                position: 1,
            },
        ],
    };
    assert!(validate(&duplicated).is_err());
}
#[test]
fn library_sort_positions_are_unique_per_category_and_cloud_ids_are_not_interchangeable() {
    use echo_api::module::playlist_sort::{self, Kind, Position, Query};
    let mut query = Query {
        total_revision: 1,
        positions: vec![
            Position {
                cloud_list_id: 1,
                kind: Kind::Owned,
                position: 0,
            },
            Position {
                cloud_list_id: 2,
                kind: Kind::Collected,
                position: 0,
            },
        ],
    };
    playlist_sort::validate(&query).unwrap();
    query.positions[1].kind = Kind::Owned;
    assert!(playlist_sort::validate(&query).is_err());
    query.positions[1].kind = Kind::Collected;
    query.positions[1].cloud_list_id = 1;
    assert!(playlist_sort::validate(&query).is_err());
}
#[test]
fn png_upload_uses_raw_boolean_result_and_a_relative_custom_reference() {
    use echo_api::module::playlist_pic_upload as upload;
    let cover = upload::convert(&sample("playlist_pic_upload/png-400.json")).unwrap();
    assert_eq!(cover.file_name, "sample-cover.png");
    assert_eq!(cover.reference, "custom/sample-cover.png");
    assert_eq!(
        upload::convert(&sample("playlist_pic_upload/rejected-width.json"))
            .unwrap_err()
            .kind,
        "business"
    );
    for filename in [
        "../cover.png",
        "custom/cover.png",
        "https://example.invalid/cover.png",
        "cover.png?query",
        "",
    ] {
        let mut malformed: Value =
            serde_json::from_slice(&sample("playlist_pic_upload/png-400.json")).unwrap();
        malformed["FileName"] = filename.into();
        assert_eq!(
            upload::convert(&serde_json::to_vec(&malformed).unwrap())
                .unwrap()
                .file_name,
            filename
        );
    }
    let mut malformed: Value =
        serde_json::from_slice(&sample("playlist_pic_upload/png-400.json")).unwrap();
    malformed["IsSuccess"] = 1.into();
    assert_eq!(
        upload::convert(&serde_json::to_vec(&malformed).unwrap())
            .unwrap()
            .file_name,
        cover.file_name
    );
}
#[test]
fn custom_cover_save_preserves_returned_read_url_shape() {
    use echo_api::module::{playlist_update, user_playlist};
    let bytes = sample("playlist_update/updated-cover.json");
    let raw: Value = serde_json::from_slice(&bytes).unwrap();
    let id = raw["data"]["info"]["listid"].as_u64().unwrap();
    playlist_update::convert(&bytes).unwrap();

    let page =
        user_playlist::convert(&sample("user_playlist/library-cover-page1.json"), 1, 100).unwrap();
    let row = page
        .page
        .items
        .iter()
        .find(|r| r.cloud_list_id == Some(id))
        .unwrap();
    assert_eq!(row.custom_cover, Some(true));
    assert!(row.cover_url.starts_with("http://c1.kgimg.com/custom/240/"));
    assert!(row.cover_url.ends_with("/sample-cover.png"));
    assert!(row.tags.is_empty());
}
#[test]
fn playlist_mutation_acknowledgements_preserve_real_versions_and_scope() {
    use echo_api::module::{
        playlist_add, playlist_del, playlist_tracks_add, playlist_tracks_del, playlist_tracks_sort,
        playlist_update,
    };
    let created = playlist_add::convert(&sample("playlist_add/created-private.json")).unwrap();
    assert!(created.total_revision > created.previous_total_revision);
    let added =
        playlist_tracks_add::convert(&sample("playlist_tracks_add/added-two.json")).unwrap();
    assert_eq!(added.song_count, Some(2));
    assert_eq!(added.outcomes.as_ref().unwrap().len(), 2);
    let duplicated =
        playlist_tracks_add::convert(&sample("playlist_tracks_add/duplicate-add.json")).unwrap();
    assert_eq!(duplicated.song_count, Some(2));
    let replaced = duplicated.replaced_file_ids.as_ref().unwrap();
    assert_eq!(
        replaced,
        &added
            .outcomes
            .as_ref()
            .unwrap()
            .iter()
            .map(|i| Some(i.file_id))
            .collect::<Vec<_>>()
    );
    assert!(
        duplicated
            .outcomes
            .as_ref()
            .unwrap()
            .iter()
            .all(|i| !replaced.contains(&Some(i.file_id)))
    );

    let sorted =
        playlist_tracks_sort::convert(&sample("playlist_tracks_sort/sorted-reverse.json")).unwrap();
    assert!(sorted.revision > sorted.previous_revision);
    let updated = playlist_update::convert(&sample("playlist_update/updated-info.json")).unwrap();
    assert!(updated.total_revision > updated.previous_total_revision);
    let removed =
        playlist_tracks_del::convert(&sample("playlist_tracks_del/removed-one.json")).unwrap();
    assert_eq!(removed.song_count, Some(1));
    assert!(removed.outcomes.is_none());
    let deleted = playlist_del::convert(
        &sample("playlist_del/deleted-owned.json"),
        created.cloud_list_id,
    )
    .unwrap();
    assert_eq!(
        deleted.playlist_count.unwrap() + 1,
        created.playlist_count.unwrap()
    );
}
#[test]
fn playlist_item_rejection_is_not_invented_success_or_missing_song_failure() {
    use echo_api::module::playlist_tracks_add;

    let rejected =
        playlist_tracks_add::convert(&sample("playlist_tracks_add/rejected-track.json")).unwrap();
    let item = &rejected.outcomes.unwrap()[0];
    assert_eq!(item.file_id, 0);
    assert_eq!(item.resource_code, 206);
    assert_eq!(rejected.revision, rejected.previous_revision);

    let accepted = playlist_tracks_add::convert(&sample(
        "playlist_tracks_add/accepted-unresolved-track.json",
    ))
    .unwrap();
    let item = &accepted.outcomes.unwrap()[0];
    assert!(item.file_id > 0);
    assert_eq!(item.song_id, Some(0));
    assert_eq!(item.resource_code, 1);
}
#[test]
fn all_six_write_authentication_errors_precede_success_dto_decoding() {
    use echo_api::module::{
        playlist_add, playlist_del, playlist_tracks_add, playlist_tracks_del, playlist_tracks_sort,
        playlist_update,
    };

    let errors = [
        playlist_add::convert(&sample("playlist_add/invalid-token.json")).unwrap_err(),
        playlist_del::convert(&sample("playlist_del/invalid-token.json"), 1).unwrap_err(),
        playlist_update::convert(&sample("playlist_update/invalid-token.json")).unwrap_err(),
        playlist_tracks_add::convert(&sample("playlist_tracks_add/invalid-token.json"))
            .unwrap_err(),
        playlist_tracks_del::convert(&sample("playlist_tracks_del/invalid-token.json"))
            .unwrap_err(),
        playlist_tracks_sort::convert(&sample("playlist_tracks_sort/invalid-token.json"))
            .unwrap_err(),
    ];
    for error in errors {
        assert_eq!(error.kind, "authentication");
        assert_eq!(error.code, Some(20017));
    }
}
#[test]
fn replacement_file_ids_keep_known_ids_and_optional_absence() {
    use echo_api::module::playlist_tracks_add;
    let mut value: Value =
        serde_json::from_slice(&sample("playlist_tracks_add/added-two.json")).unwrap();

    value["data"].as_object_mut().unwrap().remove("del_fileids");
    assert!(
        playlist_tracks_add::convert(&serde_json::to_vec(&value).unwrap())
            .unwrap()
            .replaced_file_ids
            .is_none()
    );
    value["data"]["del_fileids"] = serde_json::json!([0]);
    assert_eq!(
        playlist_tracks_add::convert(&serde_json::to_vec(&value).unwrap())
            .unwrap()
            .replaced_file_ids,
        Some(vec![Some(0)])
    );
}

#[test]
fn cloud_lists_preserve_scope_versions_and_null_last_page() {
    use echo_api::module::user_playlist;
    let first =
        user_playlist::convert(&sample("user_playlist/current-account.json"), 1, 2).unwrap();
    assert_eq!(first.page.items.len(), 2);
    assert!(first.page.items[0].cloud_list_id.is_some_and(|id| id != 0));
    assert_eq!(first.page.items[0].default_kind, Some(1));
    assert_eq!(first.page.items[0].editable, Some(true));
    let next = user_playlist::convert(&sample("user_playlist/page2.json"), 2, 2).unwrap();
    assert!(matches!(
        next.page.items[0].kind,
        user_playlist::CloudPlaylistKind::Collected
    ));
    assert_eq!(next.page.items[0].default_kind, None);
    let full = user_playlist::convert(&sample("user_playlist/full-page.json"), 1, 100).unwrap();
    assert_eq!(Some(full.page.items.len() as u64), full.page.total);
    assert_eq!(full.collection_count, Some(28));
    assert_eq!(full.page.total, Some(32)); // Collected lists are already included.
    assert!(
        user_playlist::convert(&sample("user_playlist/empty-page.json"), 10000, 2)
            .unwrap()
            .page
            .items
            .is_empty()
    );
    let error =
        user_playlist::convert(&sample("user_playlist/invalid-token.json"), 1, 2).unwrap_err();
    assert_eq!(error.kind, "authentication");
    assert_eq!(error.code, Some(20017));
}
#[test]
fn public_playlist_detail_uses_global_ids_and_exact_missing_shape() {
    use echo_api::module::playlist_detail;

    let first = playlist_detail::convert(&sample("playlist_detail/public-list.json")).unwrap();
    assert_eq!(first[0].public_id, Some(6409645));
    assert_eq!(first[0].song_count, Some(154));
    assert_eq!(
        playlist_detail::convert(&sample("playlist_detail/batch.json"))
            .unwrap()
            .len(),
        2
    );
    assert!(
        playlist_detail::convert(&sample("playlist_detail/missing.json"))
            .unwrap()
            .is_empty()
    );

    let mut broken: Value =
        serde_json::from_slice(&sample("playlist_detail/missing.json")).unwrap();
    broken["data"] = serde_json::json!({"name":"partial"});
    assert!(playlist_detail::convert(&serde_json::to_vec(&broken).unwrap()).is_err());
}
#[test]
fn public_playlist_tracks_keep_milliseconds_without_echo_offset_checks() {
    use echo_api::module::playlist_track_all;
    let first =
        playlist_track_all::convert(&sample("playlist_track_all/public-list.json"), 1, 2).unwrap();
    assert_eq!(first.items[0].song.duration_ms, Some(342047));
    assert_eq!(first.items[0].song.id, "32100648");
    assert_eq!(first.items[0].song.artists[0].id, Some(3520));
    assert_eq!(first.items[0].sort_index, Some(0));
    assert_eq!(first.items[0].song.title, "以父之名");
    assert_eq!(
        first.items[0].metadata.source_file_name,
        "周杰伦 - 以父之名"
    );
    assert_eq!(
        playlist_track_all::convert(&sample("playlist_track_all/page2.json"), 2, 2)
            .unwrap()
            .items
            .len(),
        2
    );
    assert!(
        playlist_track_all::convert(&sample("playlist_track_all/empty-page.json"), 10000, 2)
            .unwrap()
            .items
            .is_empty()
    );
    assert_eq!(
        playlist_track_all::convert(&sample("playlist_track_all/missing.json"), 1, 2)
            .unwrap_err()
            .code,
        Some(20010)
    );
    assert_eq!(
        playlist_track_all::convert(&sample("playlist_track_all/page2.json"), 1, 2)
            .unwrap()
            .items
            .len(),
        2
    );
}

#[test]
fn sampled_empty_singer_playlist_filename_displays_before_enrichment() {
    use echo_api::module::{krm_audio, playlist_track_all_new};
    let bytes = sample("playlist_track_all_new/loulan-page-2.json");
    let tracks = playlist_track_all_new::convert(&bytes).unwrap();
    let row = tracks
        .page
        .items
        .iter()
        .find(|v| v.song.id == "64323384")
        .unwrap();
    assert_eq!(
        row.metadata.source_file_name,
        "Sample Artist - [REDACTED TITLE].mp3"
    );
    assert_eq!(row.song.title, "[REDACTED TITLE]");
    assert_eq!(row.song.artists[0].name, "Sample Artist");
    assert_eq!(row.song.artists[0].id, None);
    assert_eq!(row.metadata.artists[0].name, "");
    assert_eq!(row.metadata.artists[0].id, Some(0));
    assert_eq!(
        row.metadata.filename_artist_name.as_deref(),
        Some("Sample Artist")
    );
    let metadata = krm_audio::convert(&sample("krm_audio/loulan-metadata.json")).unwrap();
    assert_eq!(metadata[0].id, row.song.id);
    assert_eq!(metadata[0].artists[0].id, Some(6743));
    assert_eq!(metadata[0].album_id, "2603117");
    // The same sampled filename field may contain separators within its title.
    let mut wire: Value = serde_json::from_slice(&bytes).unwrap();
    let raw = wire["data"]["info"]
        .as_array_mut()
        .unwrap()
        .iter_mut()
        .find(|v| v["mixsongid"] == 64323384)
        .unwrap();
    raw["name"] = "Sample Artist - Title - Live.mp3".into();
    let values = playlist_track_all_new::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(
        values
            .page
            .items
            .iter()
            .find(|v| v.song.id == "64323384")
            .unwrap()
            .song
            .title,
        "Title - Live"
    );
}

#[test]
fn playlist_filenames_keep_the_original_and_krm_presentation_uses_sampled_nulls() {
    use echo_api::module::{krm_audio, playlist_track_all_new};
    let bytes =
        sample("playlist_track_all_new/filename-cover-page-title-cover-contract-20261016.json");
    let wire: Value = serde_json::from_slice(&bytes).unwrap();

    let tracks = playlist_track_all_new::convert(&bytes).unwrap();
    assert_eq!(tracks.page.items.len(), 300);
    for (item, raw) in tracks
        .page
        .items
        .iter()
        .zip(wire["data"]["info"].as_array().unwrap())
    {
        assert_eq!(
            item.metadata.source_file_name,
            raw["name"].as_str().unwrap()
        );
        assert!(item.metadata.source_file_name.ends_with(".mp3"));
        assert!(!item.song.title.ends_with(".mp3"));
    }
    let bytes = sample("krm_audio/playlist-enrichment-title-cover-contract-20261016.json");
    let wire: Value = serde_json::from_slice(&bytes).unwrap();
    let ids: Vec<_> = wire["data"]
        .as_array()
        .unwrap()
        .iter()
        .map(|v| v["base"]["album_audio_id"].as_u64().unwrap())
        .collect();
    let values = krm_audio::convert(&bytes).unwrap();
    assert_eq!(values.len(), ids.len());
    for (value, raw) in values.iter().zip(wire["data"].as_array().unwrap()) {
        assert_eq!(value.title, raw["base"]["songname"].as_str().unwrap());
        assert_eq!(
            value.artists.len(),
            raw["authors"].as_array().unwrap().len()
        );
        if raw["album_info"].is_null() {
            assert_eq!(value.album_id, "0");
            assert!(value.cover_url.is_empty());
        } else {
            assert!(value.cover_url.starts_with("http"));
            assert!(!value.cover_url.contains("{size}"));
        }
    }
    assert!(values.iter().any(|v| v.album_id == "0"));
    let absent = sample("krm_audio/missing-catalog-title-cover-contract-20261016.json");
    assert!(krm_audio::convert(&absent).unwrap().is_empty());
    let mut malformed = wire;
    malformed["data"][0]["base"]["album_audio_id"] = serde_json::json!(999999999998u64);
    assert_eq!(
        krm_audio::convert(&serde_json::to_vec(&malformed).unwrap()).unwrap()[0].id,
        "999999999998"
    );
    malformed["data"][0] = serde_json::json!({"__status":1,"authors":[]});
    assert_eq!(
        krm_audio::convert(&serde_json::to_vec(&malformed).unwrap())
            .unwrap()
            .len(),
        values.len() - 1
    );
}
#[test]
fn cloud_tracks_map_returned_identity_and_preserve_business_failures() {
    use echo_api::module::playlist_track_all_new as module;
    let query = module::Query {
        cloud_list_id: 900001,
        page: 1,
        page_size: 1,
    };
    let first = module::convert(&sample("playlist_track_all_new/owned-list.json")).unwrap();
    assert_eq!(first.page.items[0].song.duration_ms, Some(391810));
    assert_eq!(first.page.items[0].song.privilege, Some(10));
    assert_eq!(first.page.items[0].song.id, "39691125");
    assert_eq!(first.cloud_list_id, query.cloud_list_id);
    assert_eq!(
        module::convert(&sample("playlist_track_all_new/page2.json"))
            .unwrap()
            .page
            .items
            .len(),
        1
    );
    assert!(
        module::convert(&sample("playlist_track_all_new/empty-page.json"))
            .unwrap()
            .page
            .items
            .is_empty()
    );
    assert_eq!(
        module::convert(&sample("playlist_track_all_new/invalid-token.json"))
            .unwrap_err()
            .kind,
        "authentication"
    );
    assert_eq!(
        module::convert(&sample("playlist_track_all_new/missing.json"))
            .unwrap_err()
            .code,
        Some(30203)
    );
    let mut response: Value =
        serde_json::from_slice(&sample("playlist_track_all_new/owned-list.json")).unwrap();
    response["data"]["listid"] = 6409645.into();
    assert_eq!(
        module::convert(&serde_json::to_vec(&response).unwrap())
            .unwrap()
            .cloud_list_id,
        6409645
    );
}
#[test]
fn collected_cloud_id_is_not_a_valid_cloud_track_source_and_missing_info_is_malformed() {
    use echo_api::module::{playlist_track_all_new as module, user_playlist};
    let bytes = sample("playlist_track_all_new/collected-list.json");

    let tracks = module::convert(&bytes).unwrap();
    assert_eq!(tracks.page.total, Some(0));
    assert!(tracks.page.items.is_empty());
    let public = echo_api::module::playlist_track_all::convert(
        &sample("playlist_track_all/creator-collected-list.json"),
        1,
        2,
    )
    .unwrap();
    assert_eq!(public.total, Some(24));
    assert_eq!(public.items.len(), 2);
    assert_eq!(public.items[0].song.duration_ms, Some(267964));
    let account_view = echo_api::module::playlist_track_all::convert(
        &sample("playlist_track_all/account-collected-list.json"),
        1,
        2,
    )
    .unwrap();
    assert_eq!(account_view.total, Some(0));
    assert!(
        module::convert(&sample("playlist_track_all_new/collected-list-type0.json"))
            .unwrap()
            .page
            .items
            .is_empty()
    );
    let mut broken: Value =
        serde_json::from_slice(&sample("user_playlist/empty-page.json")).unwrap();
    broken["data"].as_object_mut().unwrap().remove("info");
    assert!(user_playlist::convert(&serde_json::to_vec(&broken).unwrap(), 10000, 2).is_err());
    let mut broken: Value =
        serde_json::from_slice(&sample("playlist_track_all_new/empty-page.json")).unwrap();
    broken["data"].as_object_mut().unwrap().remove("info");
    assert!(module::convert(&serde_json::to_vec(&broken).unwrap()).is_err());
}
#[test]
fn playback_stages_have_separate_contracts_and_precise_units() {
    let user = user_verify::convert(&sample("user_verify/current-account.json")).unwrap();
    assert!(user.valid);
    assert_eq!(user.authorization, "[REDACTED]");
    let auth = song_auth::convert(&sample_response("song_url_auth_merge/128.json", 0)).unwrap();
    assert!(!auth.authorization.is_empty());
    assert!(auth.opens_at > 0);
    let standard =
        song_url_auth::convert(&sample_response("song_url_auth_merge/128.json", 1)).unwrap();
    assert_eq!(standard.duration_ms, Some(271000));
    assert_eq!(standard.standard_duration_ms, Some(271151));
    assert_eq!(standard.bit_rate_bps, Some(0)); // Sample explicitly returns 0; do not manufacture bitrate.
    assert_eq!(standard.file_size_bytes, Some(3091245));
    assert_eq!(standard.urls.len(), 2);
    assert_eq!(standard.backup_urls.len(), 1);
    let flac =
        song_url_auth::convert(&sample_response("song_url_auth_merge/flac.json", 1)).unwrap();
    assert_eq!(flac.file_size_bytes, Some(34000137));
    assert_eq!(flac.bit_rate_bps, Some(1003000));
    assert_eq!(flac.file_head, Some(100));
}
#[test]
fn sampled_automatic_and_explicit_origin_sources_keep_tracker_contract() {
    // The first response includes user verification; the second reuses that
    // authorization. Keep both protocol cases.
    for (path, auth_index, tracker_index) in [
        (
            "song_url_auth_merge/automatic-compatibility-20261013.json",
            1,
            2,
        ),
        (
            "song_url_auth_merge/automatic-origin-compatibility-20261013.json",
            0,
            1,
        ),
    ] {
        let auth = song_auth::convert(&sample_response(path, auth_index)).unwrap();
        assert!(!auth.authorization.is_empty());
        let source = song_url_auth::convert(&sample_response(path, tracker_index)).unwrap();
        assert!(!source.urls.is_empty());
        assert!(source.duration_ms > Some(0));
    }
}
#[test]
fn real_authentication_failures_are_not_success_shape_errors() {
    let error =
        user_detail::convert(&sample("user_detail/invalid-token.json"), 900000).unwrap_err();
    assert_eq!(error.kind, "authentication");
    assert_eq!(error.code, Some(20018));
    let error = match user_verify::convert(&sample("user_verify/invalid-token.json")) {
        Ok(_) => panic!("invalid token accepted"),
        Err(error) => error,
    };
    assert_eq!(error.kind, "authentication");
    assert_eq!(error.code, Some(35005));
}
#[test]
fn resource_rights_do_not_invent_playability_or_overwrite_encoded_variants() {
    let resources = privilege_lite::convert(&sample("privilege_lite/success.json")).unwrap();
    assert_eq!(resources.len(), 1);
    let resource = &resources[0];
    assert_eq!(resource.song_id, Some(32042818));
    assert_eq!(resource.primary.service_status, Some(0));
    assert_eq!(resource.primary.privilege, Some(10));
    assert_eq!(resource.primary.size_bytes, Some(4339030)); // MP3 resource stays separate from OGG.
    assert_eq!(
        resource.ogg_standard.as_ref().unwrap().size_bytes,
        Some(3091245)
    );
    let flac = resource
        .variants
        .iter()
        .find(|v| v.quality == "flac")
        .unwrap();
    assert_eq!(flac.bit_rate_bps, Some(1003000));
    assert_eq!(flac.duration_ms, Some(271107));
    assert_eq!(resource.preview.as_ref().unwrap().start_ms, Some(81000));
    assert_eq!(resource.variants.len(), 8); // Requested "super" is absent; no synthetic entry.
    let multiple = privilege_lite::convert(&sample("privilege_lite/multiple.json")).unwrap();
    assert_eq!(multiple.len(), 2);
    assert_eq!(multiple[1].song_id, Some(32042819));
    let missing = privilege_lite::convert(&sample("privilege_lite/missing.json")).unwrap();
    assert_eq!(missing[0].resource_error, Some(6));
    assert_eq!(missing[0].primary.service_status, Some(1)); // Not a valid song merely because status=1.
    assert!(missing[0].variants.is_empty());
    assert!(missing[0].ogg_standard.is_none());
    assert!(missing[0].album_title.is_none());
}
#[test]
fn sampled_resource_facts_do_not_become_account_authorization() {
    let signed_in =
        privilege_lite::convert(&sample("privilege_lite/success-rights-20261014.json")).unwrap();
    let anonymous =
        privilege_lite::convert(&sample("privilege_lite/anonymous-rights-20261014.json")).unwrap();
    assert_eq!(signed_in.len(), 1);
    assert_eq!(anonymous.len(), 1);
    // Independent real samples have identical coded resource facts. Their
    // identity conditions differ: these values alone cannot grant membership.
    for (left, right) in signed_in[0].variants.iter().zip(&anonymous[0].variants) {
        assert_eq!(left.quality, right.quality);
        assert_eq!(left.privilege, right.privilege);
        assert_eq!(left.service_status, right.service_status);
        assert_eq!(left.failure_code, right.failure_code);
        assert_eq!(left.payment_type, right.payment_type);
    }
    assert_eq!(anonymous[0].variants.len(), 8);
    let multiple = privilege_lite::convert(&sample(
        "privilege_lite/anonymous-multiple-rights-20261014.json",
    ))
    .unwrap();
    assert_eq!(multiple.len(), 2);
    assert_eq!(multiple[0].variants.len(), 8);
    assert_eq!(multiple[1].variants.len(), 7);
    assert_ne!(multiple[0].hash, multiple[1].hash);
}
#[test]
fn suggestions_keep_group_labels_and_operational_items() {
    let groups = search_suggest::convert(&sample("search_suggest/success.json"))
        .unwrap()
        .groups;
    assert_eq!(groups.len(), 3);
    assert_eq!(groups[0].title, "");
    assert_eq!(groups[0].items[0].text, "周杰伦");
    assert_eq!(groups[1].title, "MV");
    assert_eq!(groups[2].title, "专辑");
    let empty = search_suggest::convert(&sample("search_suggest/empty.json"))
        .unwrap()
        .groups;
    assert!(empty.iter().all(|group| group.items.is_empty()));
    let full = search_suggest::convert(&sample("search_suggest/artist-keyword.json"))
        .unwrap()
        .groups;
    let operation = full[0]
        .items
        .iter()
        .find(|item| item.source == "op")
        .unwrap();
    assert_eq!(operation.tags.as_ref().unwrap(), &["飙升"]);
    assert!(operation.jump.is_some());
}
#[test]
fn album_detail_missing_is_distinct_from_malformed_detail() {
    let album = album_detail::convert(&sample("album_detail/success.json"))
        .unwrap()
        .unwrap();
    assert_eq!(album.id, Some(960399));
    assert_eq!(album.title, "魔杰座");
    assert_eq!(album.artists[0].id, Some(3520));
    assert!(album.cover_url.contains("/240/"));
    assert_eq!(album.published, Some(true));
    assert_eq!(album.exclusive, Some(false));
    assert!(
        album_detail::convert(&sample("album_detail/missing.json"))
            .unwrap()
            .is_none()
    );
    let mut malformed: Value =
        serde_json::from_slice(&sample("album_detail/success.json")).unwrap();
    malformed["data"][0]
        .as_object_mut()
        .unwrap()
        .remove("album_id");
    let partial = album_detail::convert(&serde_json::to_vec(&malformed).unwrap())
        .unwrap()
        .unwrap();
    assert_eq!(partial.id, None);
    assert_eq!(partial.title, album.title);
    assert_eq!(partial.artists.len(), album.artists.len());
}
#[test]
fn artist_detail_preserves_optional_profile_counts_and_long_intro() {
    let artist = artist_detail::convert(&sample("artist_detail/known-artist.json"))
        .unwrap()
        .unwrap();
    assert_eq!(artist.id, Some(3520));
    assert_eq!(artist.name, "周杰伦");
    assert!(artist.avatar_url.contains("/240/"));
    assert_eq!(artist.birthday, "2000-01-02");
    assert_eq!(artist.fans, Some(25801816));
    assert_eq!(artist.album_count, Some(49));
    assert_eq!(artist.mv_count, Some(2826));
    assert_eq!(artist.published, Some(true));
    assert!(artist.description.contains("外文名"));
    assert!(artist.description.contains("\n\n"));
    let mut empty: Value =
        serde_json::from_slice(&sample("artist_detail/known-artist.json")).unwrap();
    empty["data"] = Value::Object(Default::default());
    assert!(
        artist_detail::convert(&serde_json::to_vec(&empty).unwrap())
            .unwrap()
            .is_none()
    );
}
#[test]
fn artist_albums_preserve_page_total_authors_and_covers() {
    let first = artist_albums::convert(&sample("artist_albums/hot-page1.json"), 1, 30).unwrap();
    assert_eq!(first.total, Some(49));
    assert_eq!(first.items.len(), 30);
    assert_eq!(first.items[0].id, Some(960399));
    assert_eq!(first.items[0].title, "魔杰座");
    assert_eq!(first.items[0].artists[0].id, Some(3520));
    assert_eq!(first.items[0].artists[0].name, "周杰伦");
    assert!(first.items[0].cover_url.contains("/240/"));
    assert_eq!(first.items[0].published_at, "2008-10-15");
    let second = artist_albums::convert(&sample("artist_albums/new-page2.json"), 2, 30).unwrap();
    assert_eq!(second.page, Some(2));
    assert_eq!(second.items.len(), 19);
    assert_eq!(
        second.items[0].artists[0].name, "周杰伦",
        "albums keep the structured author even when author_name differs"
    );
}
#[test]
fn artist_songs_preserve_total_duration_authors_and_covers() {
    let first =
        artist_audios_new::convert(&sample("artist_audios_new/new-page1.json"), 1, 100).unwrap();
    assert_eq!(first.total, Some(1848));
    assert_eq!(first.items.len(), 100);
    assert_eq!(first.items[0].id, "971554796");
    assert_eq!(
        first.items[0].title,
        "珊瑚海 (2026「嘉年华」世界巡回演唱会青岛站现场)"
    );
    assert_eq!(first.items[0].duration_ms, Some(131000));
    assert_eq!(first.items[0].artists[0].id, Some(3520));
    assert_eq!(first.items[0].artists[0].name, "周杰伦");
    assert!(
        first.items[0]
            .cover_url
            .contains("20260324203006202058.jpg")
    );
    assert_eq!(first.items[0].album_id, "0");
    let second =
        artist_audios_new::convert(&sample("artist_audios_new/new-page2.json"), 2, 100).unwrap();
    assert_eq!(second.page, Some(2));
    assert_eq!(second.items.len(), 100);
    assert!(second.items[0].id != first.items[0].id);
    let facts = &first.items[4];
    assert_eq!(facts.privilege, Some(10));
    assert_eq!(facts.metadata.payment_type, Some(3));
    assert_eq!(facts.metadata.old_copyright_code, Some(0));
    assert_eq!(facts.metadata.originality_code, Some(1));
    assert_eq!(
        facts
            .metadata
            .rights
            .as_ref()
            .and_then(|rights| rights.quality_map.as_ref())
            .and_then(|map| map.attr0),
        Some(1058193524)
    );
    let resource = |quality: &str| {
        facts
            .metadata
            .resources
            .iter()
            .find(|resource| resource.quality == quality)
    };
    assert_eq!(
        resource("flac").and_then(|resource| resource.hash.as_deref()),
        Some("AAF382638C9E300E1B1036F8AB0DBED2")
    );
    assert_eq!(
        resource("320").and_then(|resource| resource.hash.as_deref()),
        Some("D7A864D967A51EB5D39ED284F8BF85FC")
    );
    assert_eq!(
        resource("high").and_then(|resource| resource.hash.as_deref()),
        Some("2F4886D5826028AA763FF857AC78E8F6")
    );
}
#[test]
fn album_tracks_preserve_milliseconds_disc_position_and_authors() {
    let first = album_songs::convert(&sample("album_songs/success.json"), 1, 2).unwrap();
    assert_eq!(first.page.total, Some(11));
    assert_eq!(first.disc_count, Some(1));
    assert_eq!(first.page.items[0].song.id, "32042818");
    assert_eq!(first.page.items[0].song.duration_ms, Some(271151));
    assert_eq!(first.page.items[0].position, Some(1));
    let facts = &first.page.items[0];
    assert_eq!(facts.song.privilege, Some(10));
    assert_eq!(facts.metadata.payment_type, Some(3));
    assert_eq!(facts.metadata.old_copyright_code, Some(0));
    assert_eq!(facts.metadata.originality_code, Some(1));
    assert_eq!(
        facts
            .metadata
            .rights
            .as_ref()
            .and_then(|rights| rights.quality_map.as_ref())
            .and_then(|map| map.attr0),
        Some(1061142645)
    );
    assert!(facts.metadata.resources.iter().any(|resource| {
        resource.quality == "flac"
            && resource
                .hash
                .as_deref()
                .is_some_and(|hash| !hash.is_empty())
    }));
    assert!(facts.metadata.resources.iter().any(|resource| {
        resource.quality == "320"
            && resource
                .hash
                .as_deref()
                .is_some_and(|hash| !hash.is_empty())
    }));
    let second = album_songs::convert(&sample("album_songs/page2.json"), 2, 2).unwrap();
    assert_eq!(second.page.page, Some(2));
    assert_eq!(second.page.items[0].position, Some(3));
    assert_eq!(second.page.items[0].song.artists.len(), 2);
    assert_eq!(second.page.items[0].song.artists[1].id, Some(83769));
    let empty = album_songs::convert(&sample("album_songs/empty-page.json"), 20, 2).unwrap();
    assert_eq!(empty.page.total, Some(11));
    assert!(empty.page.items.is_empty());
}
#[test]
fn song_identity_duration_empty_and_second_page() {
    let SearchResults::Songs(page) =
        search::convert(SearchKind::Song, &sample("search/song.json")).unwrap()
    else {
        panic!("wrong kind")
    };
    assert_eq!(page.items.len(), 2);
    assert_eq!(page.items[0].title, "晴天");
    assert_eq!(page.items[0].id, "32100650");
    assert_eq!(page.items[0].duration_ms, Some(269000));
    assert_eq!(page.items[0].artists[0].id, Some(3520));
    let SearchResults::Songs(empty) =
        search::convert(SearchKind::Song, &sample("search/song-empty.json")).unwrap()
    else {
        panic!("wrong kind")
    };
    assert!(empty.items.is_empty());
    assert_eq!(empty.total, Some(0));
    let SearchResults::Songs(second) =
        search::convert(SearchKind::Song, &sample("search/song-page2.json")).unwrap()
    else {
        panic!("wrong kind")
    };
    assert_eq!(second.page, Some(2));
    assert_eq!(second.items[0].title, "稻香");
}
#[test]
fn all_search_variants_use_their_own_sampled_contract() {
    for (kind, path) in [
        (SearchKind::Album, "album"),
        (SearchKind::Author, "author"),
        (SearchKind::Special, "special"),
        (SearchKind::Lyric, "lyric"),
        (SearchKind::Mv, "mv"),
    ] {
        search::convert(kind, &sample(&format!("search/{path}.json"))).unwrap();
    }
    let SearchResults::Artists(artists) =
        search::convert(SearchKind::Author, &sample("search/author.json")).unwrap()
    else {
        panic!("wrong kind")
    };
    assert_eq!(artists.items[0].id, Some(3520)); // UserId=0 does not remove a real artist.
}
#[test]
fn search_incomplete_identity_and_numeric_text_keep_the_other_fields_and_rows() {
    let mut value: Value = serde_json::from_slice(&sample("search/song.json")).unwrap();
    let count = value["data"]["lists"].as_array().unwrap().len();
    value["data"]["lists"][0]
        .as_object_mut()
        .unwrap()
        .remove("MixSongID");
    let SearchResults::Songs(result) =
        search::convert(SearchKind::Song, &serde_json::to_vec(&value).unwrap()).unwrap()
    else {
        panic!("wrong kind")
    };
    assert_eq!(result.items.len(), count);
    assert_eq!(result.items[0].song.id, "");
    assert_eq!(
        result.items[0].song.title,
        value["data"]["lists"][0]["SongName"].as_str().unwrap()
    );
    value["data"]["lists"][0]["MixSongID"] = Value::from(32144422);
    let SearchResults::Songs(result) =
        search::convert(SearchKind::Song, &serde_json::to_vec(&value).unwrap()).unwrap()
    else {
        panic!("wrong kind")
    };
    assert_eq!(result.items.len(), count);
    assert_eq!(result.items[0].song.id, "32144422");
}

#[test]
fn hot_default_and_profile_are_sample_based() {
    let groups = search_hot::convert(&sample("search_hot/success.json")).unwrap();
    assert_eq!(groups[0].title, "热搜榜");
    assert!(!groups[0].keywords.is_empty());
    let default = search_default::convert(&sample("search_default/success.json")).unwrap();
    assert_eq!(default.promotions[0].title, "免费听歌");
    let profile =
        user_detail::convert(&sample("user_detail/current-account.json"), 900000).unwrap();
    assert_eq!(profile.id, 900000);
    assert_eq!(profile.nickname, "[REDACTED]");
    assert_eq!(profile.visitors, Some(9));
    assert_eq!(profile.registered_at_seconds, Some(1742865589));
    assert_eq!(profile.gender, Some(2));
    assert_eq!(profile.province, "[REDACTED]");
    assert_eq!(profile.ip_location, ""); // Actual sampled loc; not province/city.
    let mut wrong_type: Value =
        serde_json::from_slice(&sample("user_detail/current-account.json")).unwrap();
    wrong_type["data"]["hvisitors"] = "9".into();
    assert_eq!(
        user_detail::convert(&serde_json::to_vec(&wrong_type).unwrap(), 900000)
            .unwrap()
            .visitors,
        Some(9)
    );
}

#[test]
fn verification_samples_distinguish_missing_event_from_acknowledgement() {
    let error =
        echo_api::module::get_verify_info::convert(&sample("get_verify_info/invalid-event.json"))
            .err()
            .unwrap();
    assert_eq!(error.kind, "business");
    assert_eq!(error.code, Some(36001));
    let acknowledgement =
        echo_api::module::verify_user_info::convert(&sample("sidedt/invalid-event.json")).unwrap();
    // A real invalid event/proof got this acknowledgement. It is NOT valid user
    // authorization or an end-to-end CAPTCHA acceptance fixture.
    assert!(acknowledgement.acknowledged);
}

#[test]
fn real_password_login_challenge_comes_from_header_not_invented_body_metadata() {
    let bytes = sample("login/fictional-credentials-challenge.json");
    let value: Value = serde_json::from_slice(&bytes).unwrap();
    assert_eq!(value["error_code"], 20028);
    assert!(echo_api::util::verification::challenge(None, &bytes).is_none());
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/api");
    let record: Value = serde_json::from_slice(
        &fs::read(root.join("login/fictional-credentials-challenge.json")).unwrap(),
    )
    .unwrap();
    let header = record["responses"][0]["response_headers"]["ssa-code"].as_str();
    let event = echo_api::util::verification::challenge(header, &bytes).unwrap();
    assert_eq!(event.event_id, "[REDACTED]");
    assert_eq!(
        echo_api::module::login::convert(&bytes, "not-used-for-error")
            .unwrap_err()
            .code,
        Some(20028)
    );
}

#[test]
fn valid_event_configuration_preserves_provider_types_and_signed_partner_values() {
    let config = echo_api::module::get_verify_info::convert(&sample(
        "get_verify_info/password-login-event.json",
    ))
    .unwrap();
    assert_eq!(config.provider, "tencent");
    assert_eq!(config.app_id, "197787253");
    assert_eq!(config.verification_type, 23);
    assert_eq!(config.available_types, [23]);
    assert_eq!(config.business_id, Some(1000));
    assert_eq!(config.partner_status["phone"], -1);
    assert_eq!(config.session_id.as_deref(), Some("[REDACTED]"));
    assert_eq!(config.protocol_flag, Some(1));
    let mut malformed: Value =
        serde_json::from_slice(&sample("get_verify_info/password-login-event.json")).unwrap();
    malformed["data"].as_object_mut().unwrap().remove("txappid");
    assert!(
        echo_api::module::get_verify_info::convert(&serde_json::to_vec(&malformed).unwrap())
            .unwrap()
            .app_id
            .is_empty()
    );
}

#[test]
fn real_event_invalid_proof_is_not_the_invalid_event_acknowledgement() {
    let error = echo_api::module::verify_user_info::convert(&sample(
        "sidedt/password-login-event-invalid-proof.json",
    ))
    .err()
    .unwrap();
    assert_eq!(error.kind, "business");
    assert_eq!(error.code, Some(30791));
}

#[test]
fn refresh_credentials_are_explicit_and_cannot_switch_accounts() {
    let bytes = sample("login_token/current-account.json");
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/api/login_token/current-account.json");
    let record: Value = serde_json::from_slice(&fs::read(root).unwrap()).unwrap();
    let secret = serde_json::to_vec(&record["responses"][0]["decrypted_credentials"]).unwrap();
    let session = echo_api::module::login_token::convert(&bytes, &secret, 900000).unwrap();
    assert_eq!(session.user_id, 900000);
    assert_eq!(session.token, "[REDACTED]");
    assert_eq!(session.refresh_credential, "[REDACTED]");
    assert!(session.expires_at_seconds > Some(0));
    let mut partial: Value = serde_json::from_slice(&bytes).unwrap();
    partial["data"]
        .as_object_mut()
        .unwrap()
        .remove("t_expire_time");
    let without_expiry = echo_api::module::login_token::convert(
        &serde_json::to_vec(&partial).unwrap(),
        &secret,
        900000,
    )
    .unwrap();
    assert_eq!(without_expiry.expires_at_seconds, None);
    assert_eq!(without_expiry.token, session.token);

    assert_eq!(
        echo_api::module::login_token::convert(&bytes, &secret, 900001)
            .err()
            .unwrap()
            .kind,
        "authentication"
    );
    let error = echo_api::module::login_token::convert(
        &sample("login_token/invalid-token.json"),
        b"null",
        900000,
    )
    .err()
    .unwrap();
    assert_eq!(error.kind, "authentication");
    assert_eq!(error.code, Some(20017));
}
#[test]
fn qr_session_and_waiting_expiry_use_separate_sampled_contracts() {
    let session =
        echo_api::module::login_qr_key::convert(&sample("login_qr_key/device-only.json")).unwrap();
    assert_eq!(session.key, "[REDACTED]");
    assert!(session.image_data_url.starts_with("data:image/png;base64,")); // Image pixels are redacted, not a decode fixture.
    assert!(matches!(
        echo_api::module::login_qr_check::convert(&sample("login_qr_check/waiting.json"))
            .unwrap()
            .state,
        echo_api::domain::QrLoginState::Waiting
    ));
    assert!(matches!(
        echo_api::module::login_qr_check::convert(&sample("login_qr_check/invalid-key.json"))
            .unwrap()
            .state,
        echo_api::domain::QrLoginState::Expired
    ));
    let scanned = echo_api::module::login_qr_check::convert(&sample(
        "login_qr_check/interactive-state-2.json",
    ))
    .unwrap();
    assert!(matches!(
        scanned.state,
        echo_api::domain::QrLoginState::Scanned
    ));
    assert!(scanned.authorization.is_none());
    let authorized = echo_api::module::login_qr_check::convert(&sample(
        "login_qr_check/interactive-state-4.json",
    ))
    .unwrap();
    assert!(matches!(
        authorized.state,
        echo_api::domain::QrLoginState::Authorized
    ));
    let auth = authorized.authorization.unwrap();
    assert_eq!(auth.user_id, 900001);
    assert_eq!(auth.token, "[REDACTED]");
    let mut unknown: Value =
        serde_json::from_slice(&sample("login_qr_check/waiting.json")).unwrap();
    unknown["data"]["status"] = 3.into();
    assert_eq!(
        echo_api::module::login_qr_check::convert(&serde_json::to_vec(&unknown).unwrap())
            .unwrap_err()
            .kind,
        "business"
    );
}
#[test]
fn registration_acknowledgement_without_identity_is_never_registered() {
    let error =
        echo_api::module::register_dev::convert(&sample("register_dev/independent-device.json"))
            .err()
            .unwrap();
    assert_eq!(error.kind, "device_registration_unassigned");
}

#[test]
fn native_registration_success_maps_only_the_observed_dfid() {
    for path in [
        "register_dev/native-independent-device.json",
        "register_dev/native-independent-device-v3-20261026.json",
    ] {
        let device = echo_api::module::register_dev::convert(&sample(path)).unwrap();
        assert_eq!(device.dfid, "[REDACTED]");
        let mut malformed: Value = serde_json::from_slice(&sample(path)).unwrap();
        malformed["data"]["dfid"] = "-".into();
        assert_eq!(
            echo_api::module::register_dev::convert(&serde_json::to_vec(&malformed).unwrap())
                .err()
                .unwrap()
                .kind,
            "authentication"
        );
    }
}

#[test]
fn live_avatar_upload_and_membership_dates_keep_endpoint_owned_fields() {
    use echo_api::module::{user_update_avatar, user_vip_detail};
    let path = "user_update_avatar/same-current-avatar-profile-20261016.json";
    let file =
        echo_api::module::user_avatar_upload::uploaded_file(&sample_response(path, 0)).unwrap();
    let updated = user_update_avatar::convert(&sample_response(path, 1), &file).unwrap();
    assert!(updated.avatar_url.ends_with(&file));
    assert!(!updated.review_pending);
    let memberships = user_vip_detail::convert(&sample(
        "user_vip_detail/current-account-profile-dates-20261016.json",
    ))
    .unwrap();
    assert!(memberships.products.iter().any(|p| p.begins_at.is_some()));
    for product in memberships.products {
        if let Some(begin) = product.begins_at {
            assert_eq!(begin.len(), 19);
            assert_eq!(&begin[10..11], "T");
        }
    }
}

#[test]
fn account_panel_endpoints_map_their_own_raw_fields() {
    use echo_api::module::{user_grade_info, user_info, user_vip_detail};
    let identity = user_info::convert(&sample("user_info/current-account.json")).unwrap();
    assert_eq!(identity.student_status, Some(0));
    assert!(identity.student_school.is_empty());
    let serialized = serde_json::to_value(identity).unwrap();
    assert_eq!(serialized.as_object().unwrap().len(), 4);
    assert!(serialized.get("phone").is_none());
    assert!(serialized.get("token").is_none());
    let mut missing: Value =
        serde_json::from_slice(&sample("user_info/current-account.json")).unwrap();
    missing["data"]
        .as_object_mut()
        .unwrap()
        .remove("student_status");
    assert_eq!(
        user_info::convert(&serde_json::to_vec(&missing).unwrap())
            .unwrap()
            .student_status,
        None
    );

    let vip = sample("user_vip_detail/current-account.json");
    let memberships = user_vip_detail::convert(&vip).unwrap();
    assert_eq!(memberships.vip_type_code, Some(0));
    assert!(memberships.deluxe_expires_at.is_none());
    assert!(memberships.super_expires_at.is_none());
    for product in &memberships.products {
        if let Some(expiry) = &product.expires_at {
            assert_eq!(expiry.len(), 19);
            assert_eq!(&expiry[10..11], "T");
        }
    }
    assert!(
        memberships
            .products
            .iter()
            .any(|p| p.active == Some(true) && p.business == "concept" && p.product == "svip")
    );
    assert!(
        memberships
            .products
            .iter()
            .any(|p| p.active == Some(true) && p.business == "concept" && p.product == "tvip")
    );
    assert!(user_vip_detail::convert(&vip).is_ok());
    let mut invalid: Value = serde_json::from_slice(&vip).unwrap();
    invalid["data"]["busi_vip"][0]["is_vip"] = 2.into();
    assert!(user_vip_detail::convert(&serde_json::to_vec(&invalid).unwrap()).is_ok());
    // Boundary mutations test validation; they are not provider response samples.
    for date in [
        "2030-02-29 00:00:00",
        "2030-04-31 00:00:00",
        "2030-01-01 24:00:00",
    ] {
        let mut invalid: Value = serde_json::from_slice(&vip).unwrap();
        invalid["data"]["vip_end_time"] = date.into();
        assert!(user_vip_detail::convert(&serde_json::to_vec(&invalid).unwrap()).is_ok());
    }

    let bytes = sample("user_grade_info/current-account.json");
    let grade = user_grade_info::convert(&bytes).unwrap();
    assert_eq!(grade.grade, Some(5));
    assert_eq!(grade.listening_seconds, Some(864577));
    assert_eq!(grade.current_points, Some(17120));
    assert_eq!(grade.next_grade, Some(6));
    assert_eq!(grade.next_grade_points, Some(20000));
    assert!(user_grade_info::convert(&bytes).is_ok());
    let mut invalid: Value = serde_json::from_slice(&bytes).unwrap();
    invalid["data"]["d_sec"] = "864577".into();
    assert_eq!(
        user_grade_info::convert(&serde_json::to_vec(&invalid).unwrap())
            .unwrap()
            .listening_seconds,
        Some(864577)
    );
}
#[test]
fn qr_success_debug_never_contains_credentials() {
    let status = echo_api::module::login_qr_check::convert(&sample(
        "login_qr_check/interactive-state-4.json",
    ))
    .unwrap();
    let diagnostic = format!("{status:?}");
    assert!(diagnostic.contains("has_authorization: true"));
    assert!(!diagnostic.contains(&status.authorization.unwrap().token));
}

#[test]
fn social_contracts_preserve_artist_identity_and_empty_object_friends() {
    use echo_api::module::{user_fans, user_follow, user_friends, user_visitors};
    let followed = user_follow::convert(&sample("user_follow/current-account.json")).unwrap();
    assert_eq!(followed.total, Some(5));
    let artist = followed.users.iter().find(|v| v.user_id == 0).unwrap();
    assert_eq!(artist.artist_id, Some(3520));
    assert_eq!(artist.key, "artist:3520");
    assert_eq!(artist.following, None);
    let fans = user_fans::convert(&sample("user_fans/current-account.json")).unwrap();
    assert_eq!(fans.users.len(), 1);
    assert_eq!(fans.users[0].following, Some(false));
    let friends = user_friends::convert(&sample("user_friends/current-account.json")).unwrap();
    assert_eq!(friends.total, Some(0));
    assert!(friends.users.is_empty());
    let visitors = user_visitors::convert(&sample("user_visitors/current-account.json")).unwrap();
    assert_eq!(visitors.users.len(), 5);
    assert!(
        visitors
            .users
            .iter()
            .all(|v| v.visited_at_seconds.is_some() && v.following.is_none())
    );
    // Deliberate corruption tests do not constitute a nonempty upstream sample.
    let mut invalid: Value =
        serde_json::from_slice(&sample("user_friends/current-account.json")).unwrap();
    invalid["data"]["lists"] = serde_json::json!([{"userid":123}]);
    invalid["data"]["total"] = 1.into();
    let pending = user_friends::convert(&serde_json::to_vec(&invalid).unwrap()).unwrap();
    assert_eq!(pending.total, Some(1));
    assert_eq!(
        pending.unmapped_entries,
        Some(invalid["data"]["lists"].clone())
    );
    let mut invalid: Value =
        serde_json::from_slice(&sample("user_follow/current-account.json")).unwrap();
    invalid["data"]["lists"][0]["userid"] = "900001".into();
    let changed = user_follow::convert(&serde_json::to_vec(&invalid).unwrap()).unwrap();
    assert_eq!(changed.users.len(), followed.users.len());
    assert_eq!(changed.users[0].user_id, 900001);
}
#[test]
fn device_contract_matches_current_mid_and_retains_sorted_activity() {
    use echo_api::module::login_device;
    let bytes = sample("login_device/current-account.json");
    let raw: Value = serde_json::from_slice(&bytes).unwrap();
    let current = raw["data"]["li"][1]["mid"].as_str().unwrap().to_owned();
    let devices = login_device::convert(&bytes, &current).unwrap();
    assert_eq!(devices.len(), 4);
    assert!(devices[0].current);
    assert_eq!(devices[0].app_id, Some(3116));
    assert_eq!(devices[0].client_version, Some(11440));
    assert_eq!(
        devices[0].transport_fingerprint,
        raw["data"]["li"][1]["mt"].as_str().unwrap()
    );
    assert_eq!(devices.iter().filter(|v| v.current).count(), 1);
    assert!(
        devices[1..]
            .windows(2)
            .all(|v| v[0].active_at_seconds >= v[1].active_at_seconds)
    );
    let mut invalid = raw;
    invalid["data"]["li"][0]["t"] = "1791664603".into();
    let changed = login_device::convert(&serde_json::to_vec(&invalid).unwrap(), &current).unwrap();
    assert_eq!(changed.len(), devices.len());
    let device = changed
        .iter()
        .find(|d| d.id == invalid["data"]["li"][0]["mid"].as_str().unwrap())
        .unwrap();
    assert_eq!(device.active_at_seconds, Some(1791664603));
}
#[test]
fn device_kick_preserves_real_business_rejection() {
    let bytes = sample("login_device_kick/absent-target-device-removal-20261016.json");
    let error = echo_api::module::login_device_kick::convert(&bytes)
        .err()
        .unwrap();
    assert_eq!(error.kind, "business");
    assert_eq!(error.code, Some(34284));
    let success = sample("login_device_kick/fresh-disposable-session-device-removal-20261016.json");
    assert!(
        echo_api::module::login_device_kick::convert(&success)
            .unwrap()
            .removed
    );
    let expired = sample("login_device/after-cleanup-device-removal-20261016.json");
    let error = echo_api::module::login_device::convert(&expired, "sample-current")
        .err()
        .unwrap();
    assert_eq!(error.kind, "authentication");
    assert_eq!(error.code, Some(20018));
    let refreshed = sample("login_device/after-probe-device-removal-20261016.json");
    let devices = echo_api::module::login_device::convert(&refreshed, "sample-current").unwrap();
    assert_eq!(devices.len(), 10);
    assert_eq!(devices[0].login_type, Some(-1));
}
#[test]
fn profile_write_uses_observed_ack_and_explicit_clear_patch_fields() {
    use echo_api::module::user_update::{self, Query};
    assert!(
        user_update::convert(&sample("user_update/unchanged-gender.json"))
            .unwrap()
            .updated
    );
    let query = Query {
        nickname: None,
        gender: Some(0),
        birthday: Some("".into()),
        signature: Some("".into()),
        province: None,
        city: None,
    };
    let fields = user_update::fields(query.clone()).unwrap();
    assert_eq!(fields["sex"], "0");
    assert_eq!(fields["signature"], "");
    assert_eq!(fields["birthday"], "");
    assert!(!fields.contains_key("nickname"));
    let mut invalid = query;
    invalid.signature = Some("😀".repeat(61));
    assert!(user_update::fields(invalid).is_err());
    let mut invalid: Value =
        serde_json::from_slice(&sample("user_update/unchanged-gender.json")).unwrap();
    invalid["data"] = true.into();
    assert!(
        user_update::convert(&serde_json::to_vec(&invalid).unwrap())
            .unwrap()
            .updated
    );
}
#[test]
fn preferences_preserve_real_strings_unknown_ids_and_distinguish_uneditable_weights() {
    use echo_api::module::user_preference;
    let bytes = sample("user_preference/current-account.json");
    let value = user_preference::convert(&bytes).unwrap();
    assert_eq!(value.recommendation_mode.as_deref(), Some("2"));
    assert_eq!(value.gender.as_deref(), Some(""));
    assert_eq!(value.languages.weights.as_ref().unwrap().len(), 3);
    assert_eq!(value.recommendation.weights.as_ref().unwrap()["125"], 100.);
    let mut raw: Value = serde_json::from_slice(&bytes).unwrap();
    raw["data"]["song_lang"] = Value::String("malformed remote encoding".into());
    let value = user_preference::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
    assert!(value.recommendation.weights.is_none());
    assert_eq!(
        value.recommendation.encoded.as_deref(),
        Some("malformed remote encoding")
    );
    raw["data"]["song_lang"] = serde_json::json!({"125":100});
    let value = user_preference::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
    assert_eq!(value.recommendation.weights.unwrap()["125"], 100.);
    assert_eq!(value.recommendation.raw, raw["data"]["song_lang"]);
    raw["data"].as_object_mut().unwrap().remove("song_lang");
    let missing = user_preference::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
    assert!(missing.recommendation.weights.is_none());
    assert!(missing.recommendation.encoded.is_none());
    raw["data"]["song_lang"] = "".into();
    let clear = user_preference::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
    assert_eq!(clear.recommendation.encoded.as_deref(), Some(""));
    assert!(clear.recommendation.weights.unwrap().is_empty());
}
#[test]
fn preference_patch_retains_explicit_clears_and_requires_the_observed_ack() {
    use echo_api::module::user_preference_update::{self, Query};
    assert!(
        user_preference_update::convert(&sample("user_preference_update/unchanged-mode.json"))
            .unwrap()
            .updated
    );
    let fields = user_preference_update::fields(Query {
        gender: Some(String::new()),
        languages: Some(String::new()),
        ..Default::default()
    })
    .unwrap();
    assert_eq!(fields.len(), 2);
    assert_eq!(fields["gender"], "");
    assert_eq!(fields["lang"], "");
    assert!(user_preference_update::fields(Query::default()).is_err());
    assert!(serde_json::from_value::<Query>(serde_json::json!({"unrelated":"value"})).is_err());
    let mut raw: Value =
        serde_json::from_slice(&sample("user_preference_update/unchanged-mode.json")).unwrap();
    raw["data"] = Value::Bool(true);
    assert!(
        user_preference_update::convert(&serde_json::to_vec(&raw).unwrap())
            .unwrap()
            .updated
    );
}

#[test]
fn profile_identity_keeps_parseable_types_and_unknown_optional_facts() {
    let path = "user_detail/current-account-identity-20261013.json";
    let profile = user_detail::convert(&sample(path), 900000).unwrap();
    assert_eq!(profile.identity.talent_bits, Some(0));
    assert!(!profile.identity.singer);
    assert!(!profile.identity.actor);
    assert!(profile.identity.authorizations.is_empty());
    let info = echo_api::module::user_info::convert(&sample(
        "user_info/current-account-identity-20261013.json",
    ))
    .unwrap();
    assert!(info.tags.is_empty());
    let mut data: Value = serde_json::from_slice(&sample(path)).unwrap();
    data["data"]["kq_talent"] = Value::String("0".into());
    assert_eq!(
        user_detail::convert(&serde_json::to_vec(&data).unwrap(), 900000)
            .unwrap()
            .identity
            .talent_bits,
        Some(0)
    );
    data["data"].as_object_mut().unwrap().remove("kq_talent");
    assert_eq!(
        user_detail::convert(&serde_json::to_vec(&data).unwrap(), 900000)
            .unwrap()
            .identity
            .talent_bits,
        None
    );
}

#[test]
fn sampled_social_identity_includes_real_talent_singer_and_deluxe_membership() {
    let followed = echo_api::module::user_follow::convert(&sample(
        "user_follow/current-account-identity-20261013.json",
    ))
    .unwrap();
    assert_eq!(followed.users[1].identity.talent_bits, Some(66));
    assert!(followed.users[1].identity.singer);
    assert_eq!(followed.users[2].identity.talent_bits, Some(24578));
    assert_eq!(followed.users[3].user_id, 0);
    assert!(followed.users[3].identity.singer);
    let visitors = echo_api::module::user_visitors::convert(&sample(
        "user_visitors/current-account-identity-20261013.json",
    ))
    .unwrap();
    assert!(visitors.users[0].membership.deluxe_active);
    assert!(!visitors.users[0].membership.super_active);
    let fans = echo_api::module::user_fans::convert(&sample(
        "user_fans/current-account-identity-20261013.json",
    ))
    .unwrap();
    assert!(!fans.users[0].identity.singer);
    let mut invalid: Value = serde_json::from_slice(&sample(
        "user_follow/current-account-identity-20261013.json",
    ))
    .unwrap();
    invalid["data"]["lists"][1]
        .as_object_mut()
        .unwrap()
        .remove("kq_talent");
    assert!(echo_api::module::user_follow::convert(&serde_json::to_vec(&invalid).unwrap()).is_ok());
}

#[test]
fn sidebar_order_page_uses_sampled_200_page_size() {
    let bytes = sample("user_playlist/order-page-sidebar-20261013.json");
    let page = echo_api::module::user_playlist::convert(&bytes, 1, 200).unwrap();
    assert_eq!(page.page.page_size, Some(200));
    assert!(!page.page.items.is_empty());
}

#[test]
fn import_tasks_use_real_link_and_screenshot_contracts() {
    use echo_api::module::import_playlist;
    for platform in ["qishui", "kuwo", "qq", "kugou"] {
        let bytes = sample(&format!(
            "import_playlist/link-add-{platform}-20261013-shaped.json"
        ));
        let task = import_playlist::convert_created(&bytes).unwrap();
        assert_eq!(task.task_type, Some(0));
        assert!(task.task_id > 0);
        assert_eq!(
            import_playlist::convert_created(&bytes).unwrap().user_id,
            900000
        );
        for n in 0..=5 {
            let name = format!("import_playlist/link-status-{platform}-{n}-20261013-shaped.json");
            if !PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../../tests/fixtures/api")
                .join(&name)
                .exists()
            {
                continue;
            }
            let bytes = sample(&name);
            let raw: Value = serde_json::from_slice(&bytes).unwrap();
            let id: u64 = raw["data"][0]["id"].as_str().unwrap().parse().unwrap();
            let rows =
                import_playlist::convert_status(&bytes).unwrap_or_else(|e| panic!("{name}: {e:?}"));
            assert_eq!(rows.len(), 1);
            assert_eq!(rows[0].task_id, id);
            assert_eq!(import_playlist::convert_status(&bytes).unwrap().len(), 1);
            assert_eq!(
                import_playlist::convert_status(&bytes).unwrap()[0].user_id,
                900000
            );
        }
        let bytes = sample(&format!(
            "import_playlist/link-result-{platform}-20261013-shaped.json"
        ));
        let raw: Value = serde_json::from_slice(&bytes).unwrap();
        let list = raw["data"]["listid"].as_u64().unwrap_or(0);
        if platform == "qishui" {
            assert_eq!(
                import_playlist::convert_result(&bytes, 1, 100)
                    .unwrap()
                    .task
                    .status,
                11
            );
        } else {
            let result = import_playlist::convert_result(&bytes, 1, 100).unwrap();
            assert_eq!(result.task.status, 3);
            assert!(result.task.imported > Some(0));
            assert_eq!(Some(result.missed_tracks.len() as u64), result.task.missed);
            assert_eq!(
                import_playlist::convert_result(&bytes, 1, 100)
                    .unwrap()
                    .task
                    .cloud_list_id,
                list
            );
        }
    }
    let count = sample("import_playlist/task-count-sidebar-20261013-shaped.json");
    assert_eq!(
        import_playlist::convert_count(&count).unwrap()["user_id"],
        900000
    );
    assert_eq!(
        import_playlist::convert_count(&count).unwrap()["user_id"],
        900000
    );
    let bytes = sample("import_playlist/screenshot-add-20261013.json");
    assert_eq!(
        import_playlist::convert_created(&bytes).unwrap().task_type,
        Some(1)
    );
    assert_eq!(
        import_playlist::convert_upload(&sample("import_playlist/screenshot-submit-20261013.json"))
            .unwrap()["uploaded"],
        true
    );
    let bytes = sample("import_playlist/screenshot-status-3-20261013.json");
    let raw: Value = serde_json::from_slice(&bytes).unwrap();
    let id: u64 = raw["data"][0]["id"].as_str().unwrap().parse().unwrap();
    let task = import_playlist::convert_status(&bytes).unwrap().remove(0);
    assert_eq!(task.task_id, id);
    assert_eq!(task.task_type, Some(1));
    assert_eq!(task.status, 3);
    assert_eq!(task.imported, Some(1));
    assert_eq!(
        import_playlist::convert_result(
            &sample("import_playlist/screenshot-result-20261013.json"),
            1,
            100
        )
        .unwrap()
        .task
        .imported,
        Some(1)
    );
    assert!(
        import_playlist::convert_status(&sample(
            "import_playlist/missing-task-sidebar-20261013.json"
        ))
        .unwrap()
        .is_empty()
    );
    assert!(
        import_playlist::convert_result(
            &sample("import_playlist/missing-result-sidebar-20261013.json"),
            1,
            100
        )
        .is_err()
    );
}

#[test]
fn album_dynamic_cover_real_video_absence_and_url_integrity() {
    use echo_api::module::album_dycover;
    let root =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/api/album_dycover");
    let read = |name: &str| {
        let record: Value = serde_json::from_slice(&fs::read(root.join(name)).unwrap()).unwrap();
        record["raw_response"].clone()
    };
    let wire = read("dynamic-album-20261014.json");
    let result = album_dycover::convert(&serde_json::to_vec(&wire).unwrap(), 65482908)
        .unwrap()
        .unwrap();
    assert_eq!(result.album_id, "65482908");
    assert_eq!(result.urls.len(), 2);
    assert_eq!(
        result.urls[0],
        wire["data"][0]["dycover"]["h264_url"].as_str().unwrap()
    );
    assert!(result.urls[1].ends_with("&isbak=1"));
    assert!(
        album_dycover::convert(&serde_json::to_vec(&wire).unwrap(), 960399)
            .unwrap()
            .is_some()
    );
    for name in ["known-track-20261014.json", "not-found-20261014.json"] {
        assert!(
            album_dycover::convert(&serde_json::to_vec(&read(name)).unwrap(), 0)
                .unwrap()
                .is_none()
        );
    }
    let mut malformed = wire.clone();
    malformed["data"][0]["dycover"]["h264_url"] = Value::from("file:///private/video.mp4");
    assert!(
        album_dycover::convert(&serde_json::to_vec(&malformed).unwrap(), 0)
            .unwrap()
            .unwrap()
            .urls
            .iter()
            .all(|url| url.starts_with("https:") || url.starts_with("http:"))
    );
    malformed = wire;
    malformed["data"][0]["dycover"]["h264_backup_url"] = Value::Null;
    assert_eq!(
        album_dycover::convert(&serde_json::to_vec(&malformed).unwrap(), 0)
            .unwrap()
            .unwrap()
            .urls
            .len(),
        1
    );
}

#[test]
fn climax_real_string_milliseconds_absence_and_contract_mutations() {
    use echo_api::module::song_climax;
    use serde_json::json;
    let root =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/fixtures/api/song_climax");
    let read = |name: &str| {
        serde_json::from_slice::<Value>(&fs::read(root.join(name)).unwrap()).unwrap()["raw_response"].clone()
    };
    let wire = read("known-track-20261015T135030Z.json");
    let marks = song_climax::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(
        marks,
        vec![song_climax::ClimaxSegment {
            start_ms: Some(81000),
            end_ms: Some(117100),
            hash: wire["data"][0]["hash"].as_str().map(str::to_owned),
        }]
    );
    assert!(
        song_climax::convert(
            &serde_json::to_vec(&read("not-found-20261015T135030Z.json")).unwrap()
        )
        .unwrap()
        .is_empty()
    );
    // Mutations are rejection/product-boundary tests, not fabricated live samples.
    for value in [json!(81000), Value::Null, json!("not-time"), json!("-1")] {
        let mut invalid = wire.clone();
        invalid["data"][0]["start_time"] = value.clone();
        let result = song_climax::convert(&serde_json::to_vec(&invalid).unwrap());
        let result = result.unwrap();
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].start_ms, value.as_u64());
        assert_eq!(result[0].end_ms, Some(117100));
    }
    let mut invalid = wire.clone();
    invalid["data"][0]["hash"] = json!("00000000000000000000000000000000");
    let changed = song_climax::convert(&serde_json::to_vec(&invalid).unwrap()).unwrap();
    assert_eq!(changed[0].start_ms, marks[0].start_ms);
    assert_eq!(
        changed[0].hash.as_deref(),
        Some("00000000000000000000000000000000")
    );
    let mut single_tick = wire;
    single_tick["data"][0]["end_time"] = json!("80000");
    let marks = song_climax::convert(&serde_json::to_vec(&single_tick).unwrap()).unwrap();
    assert_eq!(marks[0].end_ms, Some(80000));
}

#[test]
fn original_display_fact_comes_from_sampled_song_search_flag() {
    for file in [
        "search/song-original-label-20261016.json",
        "search/lyric-track-translation-20261013.json",
    ] {
        let bytes = sample(file);
        let raw: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
        let SearchResults::Songs(page) = search::convert(SearchKind::Song, &bytes).unwrap() else {
            panic!("song kind")
        };
        for (wire, song) in raw["data"]["lists"]
            .as_array()
            .unwrap()
            .iter()
            .zip(page.items)
        {
            assert_eq!(song.original, wire["IsOriginal"].as_u64().map(|v| v == 1));
        }
    }
}

#[test]
fn search_rows_keep_payment_vip_quality_resources_and_rights() {
    let SearchResults::Songs(page) = search::convert(
        SearchKind::Song,
        &sample("search/song-original-label-20261016.json"),
    )
    .unwrap() else {
        panic!("song kind")
    };
    let first = &page.items[0];
    assert_eq!(first.privilege, Some(10));
    assert_eq!(first.metadata.payment_type, Some(3));
    assert_eq!(first.metadata.old_copyright_code, Some(0));
    assert_eq!(first.metadata.originality_code, Some(1));
    assert_eq!(
        first
            .metadata
            .rights
            .as_ref()
            .and_then(|rights| rights.quality_map.as_ref())
            .and_then(|map| map.attr0),
        Some(2134884468)
    );
    let hash = |quality: &str| {
        first
            .metadata
            .resources
            .iter()
            .find(|resource| resource.quality == quality)
            .and_then(|resource| resource.hash.as_deref())
    };
    assert_eq!(hash("320"), Some("1B56126A8A03924F1DD066259C095CBC"));
    assert_eq!(hash("flac"), Some("0A69169202DE95AAF24A9944CCF0730D"));
    assert_eq!(hash("high"), Some("D667BC5EE93F201126697CB3BA745009"));

    // Lyric search spells the same quality entries as nested objects.
    let SearchResults::Lyrics(page) =
        search::convert(SearchKind::Lyric, &sample("search/lyric.json")).unwrap()
    else {
        panic!("lyric kind")
    };
    let first = &page.items[0];
    assert_eq!(first.song.privilege, Some(10));
    assert_eq!(first.metadata.payment_type, Some(3));
    assert!(first.metadata.resources.iter().any(|resource| {
        resource.quality == "flac"
            && resource
                .hash
                .as_deref()
                .is_some_and(|hash| !hash.is_empty())
    }));
    assert!(first.metadata.resources.iter().any(|resource| {
        resource.quality == "high"
            && resource
                .hash
                .as_deref()
                .is_some_and(|hash| !hash.is_empty())
    }));
}

#[test]
fn playlist_full_page_samples_keep_rights_resources_artists_video_and_detail_facts() {
    use echo_api::module::{playlist_detail, playlist_track_all, playlist_track_all_new};
    let public = playlist_track_all::convert(
        &sample("playlist_track_all/detail-page200-playlist-detail-20261016.json"),
        1,
        200,
    )
    .unwrap();
    assert!(public.items.len() > 100);
    let first = &public.items[0];
    assert_eq!(first.metadata.audio_id, Some(317031));
    assert_eq!(first.metadata.payment_type, Some(3));
    assert_eq!(first.metadata.videos[0].track.as_deref(), Some("3"));
    assert!(first.metadata.artists[0].avatar_url.is_some());
    assert!(
        first
            .metadata
            .resources
            .iter()
            .any(|r| r.quality == "flac" && r.privilege == Some(10))
    );
    assert!(first.metadata.rights.as_ref().unwrap().preview.is_some());
    assert!(
        first
            .metadata
            .genre_map
            .as_ref()
            .unwrap()
            .contains_key("genre0")
    );
    let cloud_bytes = sample("playlist_track_all_new/detail-page300-playlist-detail-20261016.json");

    let cloud = playlist_track_all_new::convert(&cloud_bytes).unwrap();
    assert_eq!(cloud.page.items.len(), 2);
    assert_eq!(cloud.page.items[0].metadata.payment_type, Some(3));
    assert_eq!(cloud.page.items[0].metadata.failure_code, Some(4));
    assert!(cloud.page.items[0].metadata.rights.is_some());
    assert!(cloud.page.items[0].metadata.has_accompaniment.is_none());
    assert!(cloud.page.items[0].metadata.related_resources.is_empty());
    let liked_bytes = sample("playlist_track_all_new/liked-page300-playlist-detail-20261016.json");

    let liked = playlist_track_all_new::convert(&liked_bytes).unwrap();
    assert_eq!(liked.page.items.len(), 300);
    assert_eq!(
        liked
            .page
            .items
            .iter()
            .filter(|i| i.metadata.rights.as_ref().unwrap().ip_map.is_none())
            .count(),
        2
    );
    assert!(
        liked
            .page
            .items
            .iter()
            .any(|i| i.metadata.title_suffix.is_some())
    );
    assert!(
        liked
            .page
            .items
            .iter()
            .any(|i| i.metadata.rights.as_ref().unwrap().voice_ad.is_some())
    );
    let last_bytes = sample("playlist_track_all_new/liked-page2-playlist-detail-20261016.json");
    let last = playlist_track_all_new::convert(&last_bytes).unwrap();
    assert_eq!(last.page.items.len(), 40);
    let missing_details = last
        .page
        .items
        .iter()
        .find(|i| i.metadata.album_details_id == Some(0))
        .unwrap();
    assert_eq!(missing_details.song.album_id, "2603117");
    assert!(missing_details.song.album_title.is_none());
    assert!(missing_details.metadata.rights.is_none());
    assert_eq!(liked.page.total, last.page.total);

    let exponent_bytes =
        sample("playlist_track_all_new/favorite-reader-page1-playlist-detail-20261016.json");

    let exponent = playlist_track_all_new::convert(&exponent_bytes).unwrap();
    assert_eq!(
        exponent.page.items[16]
            .metadata
            .rights
            .as_ref()
            .unwrap()
            .ip_map
            .as_ref()
            .unwrap()
            .attr0,
        Some(281629629091840)
    );
    let id = "collection_3_2132029040_287_0".to_owned();
    let detail = playlist_detail::convert(&sample(
        "playlist_detail/public-list-playlist-detail-20261016.json",
    ))
    .unwrap();
    assert!(detail[0].tags.as_ref().unwrap().contains("流行"));
    assert_eq!(detail[0].created_at_seconds, Some(1673445589));
    assert_eq!(detail[0].creator_global_id.as_ref(), Some(&id));
    assert_eq!(detail[0].source_code, Some(1));
    assert_eq!(detail[0].custom_cover, Some(true));
    assert_eq!(
        detail[0].music_tags.as_ref().unwrap()[0].name.as_deref(),
        Some("流行")
    );
    let climax = echo_api::module::song_climax::convert(&sample(
        "song_climax/owned-track-climax-playlist-detail-20261016.json",
    ))
    .unwrap();
    assert_eq!(climax[0].start_ms, Some(71000));
    assert_eq!(climax[0].end_ms, Some(102300));
    let json = serde_json::to_value(&detail[0]).unwrap();
    assert!(json.get("cloud_list_id").is_none());
    let owned = playlist_detail::convert(&sample(
        "playlist_detail/owned-detail-playlist-detail-20261016.json",
    ))
    .unwrap();
    assert_eq!(owned.len(), 1);
    assert!(owned[0].public_id.is_none()); // Current default lists have no specialid.
    assert_eq!(owned[0].creator_id, Some(900000));
}

#[test]
fn decode_diagnostics_never_echo_private_values_or_dynamic_map_keys() {
    #[derive(serde::Deserialize)]
    struct Diagnostic {
        records: std::collections::BTreeMap<String, u64>,
    }
    let bad = serde_json::json!({"records":{"secret-personal-key":"secret-private-value"}});
    let err = match echo_api::util::request::decode_value::<Diagnostic>(bad, &["records"]) {
        Ok(_) => panic!("invalid value accepted"),
        Err(e) => e,
    };
    assert!(err.message.contains("records.*"));
    assert!(!err.message.contains("secret"));
    // Keep the successful typed branch covered independently of diagnostics.
    let valid = echo_api::util::request::decode_value::<Diagnostic>(
        serde_json::json!({"records":{"item":7}}),
        &["records"],
    )
    .unwrap();
    assert_eq!(valid.records["item"], 7);
}

#[test]
fn public_playlist_variations_do_not_reject_the_entire_page() {
    use echo_api::module::playlist_track_all;
    // Mutations exercise permissive mapping; they are not upstream samples.
    let mut wire: Value =
        serde_json::from_slice(&sample("playlist_track_all/public-list.json")).unwrap();
    let source = wire["data"]["songs"][0].clone();
    let mut songs = Vec::new();
    for index in 0..201 {
        let mut song = source.clone();
        song["mixsongid"] = (900000 + index).into();
        song["albuminfo"]["id"] = 0.into();
        song["albuminfo"]["name"] = "".into();
        song["has_obbligato"] = 3.into();
        song["future_extension"] = serde_json::json!({"extra": true});
        songs.push(song);
    }
    wire["data"]["songs"] = songs.into();
    wire["data"]["begin_idx"] = "different representation".into();
    wire["data"]["pagesize"] = 0.into();
    wire["data"]["count"] = 1.into();
    let result = playlist_track_all::convert(&serde_json::to_vec(&wire).unwrap(), 2, 2).unwrap();
    assert_eq!(result.items.len(), 201);
    assert_eq!(result.items[0].metadata.has_accompaniment, None);
    assert_eq!(
        result.items[0].song.album_id,
        source["album_id"].as_str().unwrap()
    );
    assert_eq!(result.items[0].metadata.album_details_id, Some(0));
    assert_eq!(
        result.items[0].song.duration_ms,
        Some(source["timelen"].as_u64().unwrap())
    );
    assert_eq!(
        result.items[0].metadata.source_file_name,
        source["name"].as_str().unwrap()
    );
}

#[test]
fn catalog_and_search_extensions_do_not_gate_existing_results() {
    use echo_api::module::{everyday_recommend, search_suggest, user_playlist};
    let mut wire: Value =
        serde_json::from_slice(&sample("user_playlist/current-account.json")).unwrap();
    wire["data"]["info"][0]["type"] = 99.into();
    wire["data"]["info"][0]["is_edit"] = 3.into();
    wire["data"]["collect_count"] = 10000.into();
    let page = user_playlist::convert(&serde_json::to_vec(&wire).unwrap(), 1, 1).unwrap();
    assert_eq!(page.page.items.len(), 2);
    assert!(matches!(
        page.page.items[0].kind,
        user_playlist::CloudPlaylistKind::Unknown
    ));
    let mut wire: Value = serde_json::from_slice(&sample("search_suggest/success.json")).unwrap();
    wire["SingerShortcut"]["id"] = 123.into();
    wire["Shortcuts"] = serde_json::json!({"extra":true});
    wire["data"][0]["RecordCount"] = 0.into();
    assert!(
        !search_suggest::convert(&serde_json::to_vec(&wire).unwrap())
            .unwrap()
            .groups
            .is_empty()
    );
    let mut wire: Value = serde_json::from_slice(&sample(
        "everyday_recommend/daily-current-pages-20261016.json",
    ))
    .unwrap();
    wire["data"]["creation_date"] = "2026-10-16".into();
    let result = everyday_recommend::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(result.date, "2026-10-16");
    assert!(!result.songs.is_empty());
}

#[test]
fn write_outcomes_preserve_new_codes_and_actual_service_errors() {
    use echo_api::module::{playlist_add, playlist_tracks_add};
    let mut wire: Value =
        serde_json::from_slice(&sample("playlist_tracks_add/added-two.json")).unwrap();
    wire["data"]["info"][0]["code"] = 777.into();
    wire["data"]["list_ver"] = 0.into();
    let result = playlist_tracks_add::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(result.outcomes.unwrap()[0].resource_code, 777);
    let mut wire: Value =
        serde_json::from_slice(&sample("playlist_add/created-private.json")).unwrap();
    wire["data"]["info"]["status"] = 0.into();
    wire["data"]["info"]["code"] = 206.into();
    let error = playlist_add::convert(&serde_json::to_vec(&wire).unwrap()).unwrap_err();
    assert_eq!(error.kind, "business");
    assert_eq!(error.code, Some(206));
}

// Derived cases below mutate captured payloads to exercise resilience; they are
// deliberately not recorded or described as successful upstream observations.
#[test]
fn partial_catalog_facts_are_retained_independently_instead_of_empty_catchalls() {
    use echo_api::module::{playlist_track_all, privilege_lite};
    let mut wire: Value =
        serde_json::from_slice(&sample("playlist_track_all/public-list.json")).unwrap();
    let original = wire["data"]["songs"][0].clone();
    wire["data"]["songs"] = serde_json::json!([original]);
    wire["data"]["songs"][0]["trans_param"] = serde_json::json!({
        "ogg_128_hash":"known-ogg-hash", "hash_offset":{"start_ms":81000,"clip_hash":"known-clip"},
        "ipmap":{"bits":"known-bits"}, "voice_ad":{"hash":"known-ad"}
    });
    let result = playlist_track_all::convert(&serde_json::to_vec(&wire).unwrap(), 1, 30).unwrap();
    let item = &result.items[0];
    let rights = item.metadata.rights.as_ref().unwrap();
    assert!(rights.quality_map.is_none());
    assert_eq!(rights.preview.as_ref().unwrap().start_ms, Some(81000));
    assert_eq!(
        rights.preview.as_ref().unwrap().clip_hash.as_deref(),
        Some("known-clip")
    );
    assert_eq!(rights.preview.as_ref().unwrap().end_ms, None);
    assert_eq!(
        rights.ip_map.as_ref().unwrap().bits.as_deref(),
        Some("known-bits")
    );
    assert_eq!(rights.ip_map.as_ref().unwrap().attr0, None);
    assert_eq!(
        rights.voice_ad.as_ref().unwrap().hash.as_deref(),
        Some("known-ad")
    );
    let resource = item
        .metadata
        .resources
        .iter()
        .find(|v| v.quality == "ogg_128")
        .unwrap();
    assert_eq!(resource.hash.as_deref(), Some("known-ogg-hash"));
    assert_eq!(resource.size_bytes, None);
    assert_eq!(
        item.song.hash,
        wire["data"]["songs"][0]["hash"].as_str().unwrap()
    );
    assert_eq!(
        item.metadata.downloads.len(),
        wire["data"]["songs"][0]["download"]
            .as_array()
            .unwrap()
            .len()
    );
    let mut wire: Value = serde_json::from_slice(&sample("privilege_lite/success.json")).unwrap();
    wire["data"][0]["trans_param"] =
        serde_json::json!({"ogg_128_filesize":12345,"hash_offset":{"end_ms":90000}});
    let result = privilege_lite::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(result[0].ogg_standard.as_ref().unwrap().hash, None);
    assert_eq!(
        result[0].ogg_standard.as_ref().unwrap().size_bytes,
        Some(12345)
    );
    assert_eq!(result[0].preview.as_ref().unwrap().end_ms, Some(90000));
    assert!(result[0].quality_map.is_none());
    assert_eq!(
        result[0].primary.hash,
        wire["data"][0]["hash"].as_str().unwrap()
    );
}

#[test]
fn catalog_missing_privilege_and_partial_tags_keep_rows_and_all_known_values() {
    use echo_api::module::{everyday_recommend, rank_audio};
    let mut wire: Value = serde_json::from_slice(&sample(
        "everyday_recommend/daily-current-pages-20261016.json",
    ))
    .unwrap();
    let row = &mut wire["data"]["song_list"][0];
    row.as_object_mut().unwrap().remove("privilege");
    row["ips_tags"] = serde_json::json!({"name":"partial-tag"});
    row["relate_goods"] = serde_json::json!({"provider_extension":[1,2]});
    row["hash_flac"] = "".into();
    row["filesize_flac"] = 12345.into();
    let result = everyday_recommend::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(
        result.songs.len(),
        wire["data"]["song_list"].as_array().unwrap().len()
    );
    let song = &result.songs[0];
    assert_eq!(song.privilege, None);
    assert_eq!(
        song.metadata.ip_tags[0].name.as_deref(),
        Some("partial-tag")
    );
    assert_eq!(song.metadata.ip_tags[0].id, None);
    assert_eq!(
        song.metadata.related_goods_extension.as_ref().unwrap(),
        &wire["data"]["song_list"][0]["relate_goods"]
    );
    assert!(
        song.metadata
            .resources
            .iter()
            .any(|r| r.quality == "flac" && r.size_bytes == Some(12345))
    );
    let mut wire: Value =
        serde_json::from_slice(&sample("rank_audio/rank-first-pages-20261016.json")).unwrap();
    wire["data"]["songlist"][0]["privilege_download"]
        .as_object_mut()
        .unwrap()
        .remove("privilege");
    let result = rank_audio::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(
        result.songs.len(),
        wire["data"]["songlist"].as_array().unwrap().len()
    );
    assert_eq!(result.songs[0].privilege, None);
    assert_eq!(
        result.songs[0].title,
        wire["data"]["songlist"][0]["songname"].as_str().unwrap()
    );
}

#[test]
fn source_codes_extensions_and_social_counts_survive_ui_projections() {
    use echo_api::module::{
        search_default, search_suggest, user_follow, user_playlist, user_visitors,
    };
    let mut wire: Value =
        serde_json::from_slice(&sample("user_playlist/current-account.json")).unwrap();
    wire["data"]["info"][0]["type"] = 99.into();
    wire["data"]["info"][0]["is_edit"] = 3.into();
    let result = user_playlist::convert(&serde_json::to_vec(&wire).unwrap(), 1, 30).unwrap();
    assert_eq!(result.page.items[0].kind_code, Some(99));
    assert_eq!(result.page.items[0].flag_codes.editable, Some(3));
    let mut wire: Value =
        serde_json::from_slice(&sample("user_follow/current-account.json")).unwrap();
    wire["data"]["lists"][0]["is_friend"] = 3.into();
    wire["data"]["lists"][0]["vip_type"] = Value::Null;
    let result = user_follow::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(result.users[0].mutual, None);
    assert_eq!(result.users[0].identity_facts.friend_code, Some(3));
    assert_eq!(result.users[0].identity_facts.vip_type, None);
    let bytes = sample("user_visitors/current-account.json");
    let wire: Value = serde_json::from_slice(&bytes).unwrap();
    let result = user_visitors::convert(&bytes).unwrap();
    assert_eq!(
        result.next_page,
        wire["data"]["next"].as_u64().map(|v| v as u32)
    );
    assert_eq!(result.visitor_count, wire["data"]["hvisitors"].as_u64());
    let mut wire: Value = serde_json::from_slice(&sample("search_suggest/success.json")).unwrap();
    wire["data"][0]["RecordCount"] = 99.into();
    wire["SingerShortcut"] = serde_json::json!({"id":123});
    wire["Shortcuts"] = serde_json::json!({"new_value":true});
    let result = search_suggest::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(result.groups[0].reported_count, Some(99));
    assert_eq!(
        result.singer_shortcut_extension,
        Some(wire["SingerShortcut"].clone())
    );
    assert_eq!(result.shortcuts_extension, Some(wire["Shortcuts"].clone()));
    let bytes = sample("search_default/success.json");
    let mut wire: Value = serde_json::from_slice(&bytes).unwrap();
    wire["data"]["fallback"] = serde_json::json!({"new_value":true});
    let result = search_default::convert(&serde_json::to_vec(&wire).unwrap()).unwrap();
    assert_eq!(
        result.fallback_extension,
        Some(wire["data"]["fallback"].clone())
    );
    assert_eq!(
        result.promotions.len(),
        wire["data"]["ads"].as_array().unwrap().len()
    );
}

#[test]
fn content_blacklist_real_samples_keep_keys_metadata_and_optional_records() {
    use echo_api::module::{
        blacklist,
        blacklist_list::{self, Label},
    };
    for (label, name) in [(Label::Song, "song"), (Label::Singer, "singer")] {
        let empty = blacklist_list::convert(
            &sample(&format!(
                "blacklist_list/current-{name}-blacklist-20261016.json"
            )),
            label,
        )
        .unwrap();
        assert_eq!(empty.total, Some(0));
        assert!(empty.items.is_empty());
        let page = blacklist_list::convert(
            &sample(&format!(
                "blacklist_list/after-add-{name}-blacklist-20261016.json"
            )),
            label,
        )
        .unwrap();
        assert_eq!(page.total, Some(1));
        assert_eq!(page.page, Some(1));
        assert_eq!(page.page_size, Some(30));
        assert_eq!(page.items.len(), 1);
        let item = &page.items[0];
        assert!(!item.key.as_deref().unwrap().is_empty());
        assert_eq!(item.name.as_deref(), Some("Sample blocked item"));
        assert!(item.created_at.as_deref().unwrap().parse::<u64>().unwrap() > 0);
        if label == Label::Song {
            assert_eq!(item.mix_song_id.as_deref(), Some("900001"));
        } else {
            assert!(item.mix_song_id.is_none());
        }
        let end = blacklist_list::convert(
            &sample(&format!(
                "blacklist_list/after-end-{name}-blacklist-20261016.json"
            )),
            label,
        )
        .unwrap();
        assert_eq!(end.total, Some(1));
        assert_eq!(end.page, Some(2));
        assert!(end.items.is_empty());
        for operation in ["add", "remove"] {
            assert!(
                blacklist::convert(&sample(&format!(
                    "blacklist/{operation}-{name}-blacklist-20261016.json"
                )))
                .unwrap()
                .updated
            );
        }
        let restored = blacklist_list::convert(
            &sample(&format!(
                "blacklist_list/after-restore-{name}-blacklist-20261016.json"
            )),
            label,
        )
        .unwrap();
        assert_eq!(restored.total, Some(0));
        assert!(restored.items.is_empty());
    }
    // Offline perturbations verify nullable metadata and row retention; never
    // represented as observations from the real provider.
    let mut raw: Value = serde_json::from_slice(&sample(
        "blacklist_list/after-add-song-blacklist-20261016.json",
    ))
    .unwrap();
    raw["data"]["items"][0]["song_v"] = "invalid encoded metadata".into();
    raw["data"]["items"]
        .as_array_mut()
        .unwrap()
        .push(serde_json::json!({"song_v":"{\"n\":\"Name only\"}","extension":7}));
    raw["data"]["total"] = Value::Null;
    raw["data"]["source"] = 999.into();
    let page = blacklist_list::convert(&serde_json::to_vec(&raw).unwrap(), Label::Song).unwrap();
    assert_eq!(page.items.len(), 2);
    assert!(page.total.is_none());
    assert!(page.items[0].name.is_none());
    assert!(page.items[0].mix_song_id.is_none());
    assert_eq!(
        page.items[0].source_record["song_v"],
        "invalid encoded metadata"
    );
    assert!(page.items[1].key.is_none());
    assert_eq!(page.items[1].name.as_deref(), Some("Name only"));
    assert_eq!(page.items[1].source_record["extension"], 7);
    raw["data"]["total"] = "unknown counter".into();
    raw["data"]["page"] = (-7).into();
    raw["data"]["pagesize"] = "30".into();
    let optional =
        blacklist_list::convert(&serde_json::to_vec(&raw).unwrap(), Label::Song).unwrap();
    assert_eq!(optional.items.len(), 2);
    assert!(optional.total.is_none());
    assert!(optional.page.is_none());
    assert_eq!(optional.page_size, Some(30));
    assert_eq!(optional.provider_fields["total"], "unknown counter");
    assert_eq!(optional.provider_fields["page"], -7);
    raw["status"] = 0.into();
    raw["error_code"] = 20018.into();
    raw["data"] = "".into();
    assert_eq!(
        blacklist_list::convert(&serde_json::to_vec(&raw).unwrap(), Label::Song)
            .err()
            .unwrap()
            .code,
        Some(20018)
    );
}

#[path = "support/response_variants.rs"]
mod response_variants;

#[test]
fn b3_cloud_video_purchased_follow_sampled_shapes() {
    use echo_api::module::{
        user_cloud, user_cloud_del, user_purchased_albums, user_purchased_songs, user_video_collect,
    };
    // Cloud page 1: the sampled row keeps file (kv_id) and album-audio
    // identities apart; the provider end-of-list code 20010 yields an empty
    // page instead of a contract failure.
    let cloud = user_cloud::convert(&sample("user_cloud/page1.json"), 1, 30).unwrap();
    assert_eq!(cloud.page.items.len(), 28);
    assert_eq!(cloud.counters.list_count, Some(28));
    assert_eq!(cloud.counters.used_bytes, Some(865199039));
    let row = &cloud.page.items[0];
    assert_eq!(row.file_id, Some(66));
    assert_eq!(row.song.id, "38232532");
    assert_eq!(row.song.album_id, "1594824");
    assert_eq!(row.song.duration_ms, Some(224653));
    assert_eq!(row.song.hash, "0123456789ABCDEF0123456789ABCDEF");
    assert!(!row.song.title.is_empty());
    assert_eq!(row.song.artists.len(), 1);
    assert_eq!(row.metadata.artists.len(), row.song.artists.len());
    assert_eq!(row.metadata.artists[0].id, row.song.artists[0].id);
    assert_eq!(row.metadata.artists[0].name, row.song.artists[0].name);
    assert!(
        row.metadata.artists[0]
            .avatar_url
            .as_deref()
            .is_some_and(|url| !url.is_empty())
    );
    let end = user_cloud::convert(&sample("user_cloud/page2.json"), 2, 30).unwrap();
    assert!(end.page.items.is_empty());
    assert_eq!(end.counters.list_count, None);
    let invalid = user_cloud::convert(&sample("user_cloud/invalid-token.json"), 1, 30);
    assert!(matches!(invalid, Err(e) if e.kind == "authentication"));
    // Deletion ack carries only counters; callers verify by re-reading.
    let deleted = user_cloud_del::convert(&sample("user_cloud_del/owned-test-file.json")).unwrap();
    assert_eq!(deleted.counters.list_count, Some(28));
    assert_eq!(deleted.counters.list_revision, Some(78));
    // Collected videos: per-quality hash/size facts and the first related song.
    let videos =
        user_video_collect::convert(&sample("user_video_collect/page1.json"), 1, 30).unwrap();
    assert_eq!(videos.page.items.len(), 6);
    assert_eq!(videos.page.total, Some(6));
    assert_eq!(videos.revision, Some(14));
    let video = &videos.page.items[0];
    assert_eq!(video.id, Some(590810));
    assert_eq!(video.variants.len(), 8);
    let related = video.related_song.as_ref().unwrap();
    assert_eq!(related.album_audio_id, Some(27517488));
    let empty =
        user_video_collect::convert(&sample("user_video_collect/page2.json"), 2, 30).unwrap();
    assert!(empty.page.items.is_empty());
    // Empty purchased containers remain valid after non-empty row support.
    let songs =
        user_purchased_songs::convert(&sample("user_purchased_songs/page1.json"), 1, 30).unwrap();
    assert_eq!(songs.total, Some(0));
    assert!(songs.items.is_empty());
    let albums =
        user_purchased_albums::convert(&sample("user_purchased_albums/page1.json"), 1, 30).unwrap();
    assert_eq!(albums.total, Some(0));
    assert!(albums.items.is_empty());
    // Follow rows now preserve the raw iden_type used by the favorites split.
    let followed =
        echo_api::module::user_follow::convert(&sample("user_follow/current-account.json"))
            .unwrap();
    let singer_row = followed
        .users
        .iter()
        .find(|v| v.artist_id == Some(810483))
        .unwrap();
    assert_eq!(singer_row.identity_facts.identity_type, Some(1));
    let user_row = followed.users.iter().find(|v| v.user_id == 900001).unwrap();
    assert_eq!(user_row.identity_facts.identity_type, Some(0));
}

#[test]
fn gateway_playback_and_independent_grade_write_match_new_live_captures() {
    let user =
        user_verify::convert(&sample("user_verify/current-account-gateway-20261018.json")).unwrap();
    assert!(user.valid);
    let auth = song_auth::convert(&sample_response(
        "song_url_auth_merge/128-gateway-20261018.json",
        0,
    ))
    .unwrap();
    assert!(!auth.authorization.is_empty());
    let source = song_url_auth::convert(&sample_response(
        "song_url_auth_merge/128-gateway-20261018.json",
        1,
    ))
    .unwrap();
    assert_eq!(source.duration_ms, Some(271000));
    assert_eq!(source.standard_duration_ms, Some(271151));
    assert_eq!(source.file_size_bytes, Some(3091245));
    let before = echo_api::module::user_grade_info::convert(&sample(
        "user_grade_info/before-real-baseline-zero-increment-20261018.json",
    ))
    .unwrap();
    let written = echo_api::module::user_grade_info::convert(&sample(
        "user_grade_info/real-baseline-zero-increment-20261018.json",
    ))
    .unwrap();
    let after = echo_api::module::user_grade_info::convert(&sample(
        "user_grade_info/after-real-baseline-zero-increment-20261018.json",
    ))
    .unwrap();
    assert_eq!(before.listening_seconds, Some(937281));
    assert_eq!(written.listening_seconds, before.listening_seconds);
    assert_eq!(after.listening_seconds, before.listening_seconds);
    assert_eq!(written.grade, Some(5));
    assert_eq!(written.current_points, Some(18327));
}

#[test]
fn purchased_non_empty_singles_keep_audio_identity_and_all_quality_facts() {
    use echo_api::module::user_purchased_songs;
    let bytes = sample("user_purchased_songs/next-current-page1-20261028.json");
    let raw: Value = serde_json::from_slice(&bytes).unwrap();
    let songs = user_purchased_songs::convert(&bytes, 1, 30).unwrap();
    assert_eq!(songs.total, Some(2));
    assert_eq!(songs.items.len(), 2);
    for (song, row) in songs
        .items
        .iter()
        .zip(raw["data"]["goods"].as_array().unwrap())
    {
        let audio = &row["audio_info"];
        assert_eq!(song.song.id, row["album_audio_id"].to_string());
        assert_ne!(song.song.id, row["good_scid"].to_string());
        assert_eq!(row["hash"], "");
        assert_eq!(song.song.hash, audio["hash_high"].as_str().unwrap());
        assert_eq!(song.song.title, row["songname"].as_str().unwrap());
        assert_eq!(song.song.album_id, audio["album_id"].to_string());
        assert_eq!(song.song.duration_ms, audio["duration"].as_u64());
        assert_eq!(song.song.artists.len(), 1);
        assert!(song.song.artists[0].id.is_none());
        assert!(song.song.cover_url.contains("/400/"));
        assert_eq!(song.metadata.catalog.resources.len(), 4);
        for (resource, quality) in song
            .metadata
            .catalog
            .resources
            .iter()
            .zip(["128", "320", "flac", "high"])
        {
            assert_eq!(resource.quality, quality);
            assert_eq!(
                resource.hash.as_deref(),
                audio[format!("hash_{quality}")].as_str()
            );
            assert_eq!(
                resource.size_bytes,
                audio[format!("filesize_{quality}")].as_u64()
            );
            assert_eq!(
                resource.privilege.map(u64::from),
                audio[format!("privilege_{quality}")].as_u64()
            );
        }
        assert_eq!(
            song.metadata.catalog.resources[0].bit_rate_bps,
            Some(128000)
        );
        assert!(song.metadata.catalog.resources[1].bit_rate_bps.is_none());
        let rights = song.metadata.catalog.rights.as_ref().unwrap();
        assert!(rights.quality_map.is_some());
        assert_eq!(rights.preview.as_ref().unwrap().end_ms, Some(60000));
        assert_eq!(
            song.metadata.provider_extension["purchase"]["good_scid"],
            row["good_scid"]
        );
        assert_eq!(
            song.metadata.provider_extension["trans_param"]["cid"],
            audio["trans_param"]["cid"]
        );
    }
}

#[test]
fn purchased_albums_and_real_pagination_preserve_card_ids_and_end_pages() {
    use echo_api::module::{user_purchased_albums, user_purchased_songs};
    let bytes = sample("user_purchased_albums/next-current-page1-20261028.json");
    let raw: Value = serde_json::from_slice(&bytes).unwrap();
    let albums = user_purchased_albums::convert(&bytes, 1, 30).unwrap();
    assert_eq!(albums.total, Some(6));
    assert_eq!(albums.items.len(), 6);
    for (album, row) in albums
        .items
        .iter()
        .zip(raw["data"]["goods"].as_array().unwrap())
    {
        assert_eq!(album.album.id, row["album_id"].as_u64());
        assert_eq!(album.album.title, row["album_name"].as_str().unwrap());
        assert_eq!(
            album.album.artists[0].name,
            row["singer_name"].as_str().unwrap()
        );
        assert!(album.album.artists[0].id.is_none());
        assert!(album.album.cover_url.contains("/400/"));
        assert!(album.album.song_count.is_none());
        assert!(album.album.published_at.is_empty());
        assert_eq!(album.publication_code, Some(1));
        assert_eq!(album.provider_extension["buy_total"], row["buy_total"]);
    }
    let songs = user_purchased_songs::convert(
        &sample("user_purchased_songs/next-page2-size1-20261028.json"),
        2,
        1,
    )
    .unwrap();
    assert_eq!(songs.page, Some(2));
    assert_eq!(songs.items.len(), 1);
    assert!(
        user_purchased_songs::convert(
            &sample("user_purchased_songs/next-page3-size1-20261028.json"),
            3,
            1
        )
        .unwrap()
        .items
        .is_empty()
    );
    let albums = user_purchased_albums::convert(
        &sample("user_purchased_albums/next-page2-size4-20261028.json"),
        2,
        4,
    )
    .unwrap();
    assert_eq!(albums.page_size, Some(4));
    assert_eq!(albums.items.len(), 2);
    assert!(
        user_purchased_albums::convert(
            &sample("user_purchased_albums/next-page3-size4-20261028.json"),
            3,
            4
        )
        .unwrap()
        .items
        .is_empty()
    );
}

#[test]
fn song_metadata_observed_batch_limit_rejections_preserve_business_error() {
    use echo_api::module::krm_audio;
    for n in [51, 100] {
        let bytes = sample(&format!("krm_audio/boundary-size{n}-20261029.json"));
        let err = krm_audio::convert(&bytes).unwrap_err();
        assert_eq!(err.kind, "business");
        assert_eq!(err.code, Some(20010));
    }
    let records = krm_audio::convert(&sample("krm_audio/batch-size50-20261029.json")).unwrap();
    assert_eq!(records.len(), 50);
    assert!(
        records
            .iter()
            .all(|v| !v.id.is_empty() && !v.title.is_empty())
    );
    let current = krm_audio::convert(&sample("krm_audio/restored-current-20261029.json")).unwrap();
    assert_eq!(current.len(), 1);
    assert_eq!(current[0].id, "252666919");
    assert_eq!(current[0].artists.len(), 2);
    assert!(!current[0].cover_url.is_empty());
}
