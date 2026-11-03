//! These fixtures are transport captures, not old mappers or invented successes.
use echo_api::module::{
    ai_recommend_song, everyday_recommend, everyday_style_recommend, home_discover, rank_audio,
    rank_list, rank_top, top_ip, top_playlist,
};
use serde_json::Value;
use std::{fs, path::PathBuf};
fn raw(module: &str, name: &str) -> Value {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/api")
        .join(module)
        .join(format!("{name}-pages-20261016.json"));
    let record: Value = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
    record["responses"].as_array().unwrap().last().unwrap()["payload"].clone()
}
fn bytes(v: &Value) -> Vec<u8> {
    serde_json::to_vec(v).unwrap()
}
#[test]
fn playlist_category_count_and_editor_filter_are_not_guessed() {
    for category in ["home-default", "home-hires"] {
        let v = raw("top_playlist", category);
        let cards = top_playlist::convert(&bytes(&v)).unwrap();
        assert_eq!(
            cards.len(),
            v["data"]["special_list"].as_array().unwrap().len()
        );
        assert!(cards.iter().all(|p| p.song_count.is_none()
            && !p.id.is_empty()
            && !p.cover_url.contains("{size}")));
    }
    let v = raw("top_ip", "home-editor");
    let cards = top_ip::convert(&bytes(&v)).unwrap();
    let expected = v["data"]["list"]
        .as_array()
        .unwrap()
        .iter()
        .filter(|v| v["type"] == 1 && v["extra"]["global_collection_id"].is_string())
        .count();
    assert_eq!(cards.len(), expected);
    assert!(expected > 0);
}
#[test]
fn live_category_captures_keep_populated_and_omitted_lists_distinct() {
    for (name, expected) in [
        ("current-587-0-20261103.json", 30),
        ("current-660-1-20261103.json", 0),
    ] {
        let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/api/top_playlist")
            .join(name);
        let record: Value = serde_json::from_slice(&fs::read(path).unwrap()).unwrap();
        assert_eq!(record["http_status"], 200);
        let payload = &record["payload"];
        assert_eq!(payload["status"], 1);
        assert_eq!(payload["error_code"], 0);
        let cards = top_playlist::convert(&bytes(payload)).unwrap();
        assert_eq!(cards.len(), expected);
        if expected == 0 {
            assert!(payload["data"].get("special_list").is_none());
            assert!(payload["data"].get("OlexpIds").is_some());
        } else {
            for (card, wire) in cards
                .iter()
                .zip(payload["data"]["special_list"].as_array().unwrap())
            {
                assert_eq!(card.id, wire["global_collection_id"].as_str().unwrap());
                assert_eq!(card.title, wire["specialname"].as_str().unwrap());
            }
        }
    }
}
#[test]
fn daily_current_and_anonymous_preserve_seconds_and_metadata() {
    for name in ["daily-current", "daily-anonymous"] {
        let v = raw("everyday_recommend", name);
        for (index, song) in v["data"]["song_list"]
            .as_array()
            .unwrap()
            .iter()
            .enumerate()
        {
            let _: echo_api::util::recommendation_song::Song = serde_json::from_value(song.clone())
                .unwrap_or_else(|error| panic!("{name} song {index}: {error}"));
        }
        let result = everyday_recommend::convert(&bytes(&v)).unwrap();
        assert_eq!(result.date, v["data"]["creation_date"].as_str().unwrap());
        assert_eq!(
            result.songs[0].duration_ms,
            Some(v["data"]["song_list"][0]["time_length"].as_u64().unwrap() * 1000)
        );
        assert_eq!(
            result.songs[0].id,
            v["data"]["song_list"][0]["album_audio_id"]
                .as_str()
                .unwrap()
        );
    }
}
#[test]
fn style_default_anonymous_and_selected_tag_have_independent_captures() {
    for name in [
        "home-style",
        "home-style-anonymous",
        "home-style-selected",
        "home-style-multiple",
        "home-style-multiple-anonymous",
        "home-style-default-plus",
        "home-style-default-plus-anonymous",
    ] {
        let v = raw("everyday_style_recommend", name);
        for (index, song) in v["data"]["song_list"]
            .as_array()
            .unwrap()
            .iter()
            .enumerate()
        {
            let _: echo_api::util::recommendation_song::Song = serde_json::from_value(song.clone())
                .unwrap_or_else(|error| panic!("{name} song {index}: {error}"));
        }
        let result = everyday_style_recommend::convert(&bytes(&v)).unwrap();
        assert_eq!(
            result.songs.len(),
            v["data"]["song_list"].as_array().unwrap().len()
        );
        assert_eq!(
            result.groups.len(),
            v["data"]["tag_info"].as_array().unwrap().len()
        );
        assert!(
            result
                .groups
                .iter()
                .any(|g| g.child.iter().any(|t| t.id == "S15"))
        );
    }
}
#[test]
fn rank_sources_and_millisecond_audio_are_separate_contracts() {
    let recommended = rank_top::convert(&bytes(&raw("rank_top", "rank-recommended"))).unwrap();
    let catalog = rank_list::convert(&bytes(&raw("rank_list", "rank-catalog"))).unwrap();
    assert!(recommended[0].published_at.is_none());
    assert!(catalog[0].published_at.is_some());
    for name in ["rank-songs", "rank-first"] {
        let v = raw("rank_audio", name);
        let result = rank_audio::convert(&bytes(&v)).unwrap();
        assert_eq!(
            result.songs.len(),
            v["data"]["songlist"].as_array().unwrap().len()
        );
        assert_eq!(
            result.songs[0].duration_ms,
            Some(
                v["data"]["songlist"][0]["audio_info"]["duration_128"]
                    .as_u64()
                    .unwrap()
            )
        );
    }
}
#[test]
fn scalar_variants_and_optional_absence_keep_rows_and_real_business_errors() {
    let mut v = raw("everyday_recommend", "daily-current");
    v["data"]["song_list"][0]["album_audio_id"] = serde_json::json!(12);
    assert_eq!(
        everyday_recommend::convert(&bytes(&v)).unwrap().songs[0].id,
        "12"
    );
    let mut v = raw("rank_audio", "rank-songs");
    v["data"]["songlist"][0]["audio_info"]["duration_128"] = serde_json::json!("156000");
    assert_eq!(
        rank_audio::convert(&bytes(&v)).unwrap().songs[0].duration_ms,
        Some(156000)
    );
    let mut v = raw("top_playlist", "home-default");
    v["data"]["special_list"][0]["global_collection_id"] = Value::Null;
    let result = top_playlist::convert(&bytes(&v)).unwrap();
    assert_eq!(
        result.len(),
        v["data"]["special_list"].as_array().unwrap().len()
    );
    assert_eq!(result[0].id, "");
    let mut v = raw("everyday_style_recommend", "home-style");
    v["status"] = serde_json::json!(0);
    v["error_code"] = serde_json::json!(152);
    assert_eq!(
        everyday_style_recommend::convert(&bytes(&v))
            .err()
            .unwrap()
            .kind,
        "business"
    );
}

#[test]
fn full_functional_metadata_survives_conversion_without_opaque_tracking() {
    for (module, name) in [
        ("everyday_recommend", "daily-current"),
        ("everyday_style_recommend", "home-style"),
        ("everyday_style_recommend", "home-style-selected"),
    ] {
        let raw = raw(module, name);
        let result = if module == "everyday_recommend" {
            serde_json::to_value(everyday_recommend::convert(&bytes(&raw)).unwrap()).unwrap()
        } else {
            serde_json::to_value(everyday_style_recommend::convert(&bytes(&raw)).unwrap()).unwrap()
        };
        let source = raw["data"]["song_list"].as_array().unwrap();
        let target = result["songs"].as_array().unwrap();
        for (wire, song) in source.iter().zip(target) {
            let m = &song["metadata"];
            assert_eq!(m["audio_id"], wire["songid"]);
            assert_eq!(m["payment_type"], wire["pay_type"]);
            assert_eq!(m["failure_code"], wire["fail_process"]);
            assert_eq!(m["old_copyright_code"], wire["old_cpy"]);
            assert_eq!(m["original_title"], wire["ori_audio_name"]);
            assert_eq!(m["album_audio_remark"], wire["album_audio_remark"]);
            assert_eq!(
                m["rights"]["quality_map"],
                wire["trans_param"]["qualitymap"]
            );
            assert_eq!(
                m["rights"]["copyright_scope"],
                wire["trans_param"]["cpy_map"]
            );
            assert_eq!(
                m["artists"].as_array().unwrap().len(),
                wire["singerinfo"].as_array().unwrap().len()
            );
            assert_eq!(
                m["tags"].as_array().unwrap().len(),
                wire["tags"].as_array().unwrap().len()
            );
            if wire["mv_hash"] != "" {
                assert_eq!(m["videos"][0]["hash"], wire["mv_hash"]);
            }
            for key in [
                "hash_128",
                "hash_192",
                "hash_320",
                "hash_flac",
                "hash_ape",
                "hash_other",
            ] {
                if wire[key] != "" {
                    assert!(
                        m["resources"]
                            .as_array()
                            .unwrap()
                            .iter()
                            .any(|r| r["hash"] == wire[key])
                    );
                }
            }
            assert!(m.get("alg_path").is_none());
            assert!(m.get("mid").is_none());
        }
    }
    for name in ["rank-first", "rank-songs"] {
        let raw = raw("rank_audio", name);
        let result = serde_json::to_value(rank_audio::convert(&bytes(&raw)).unwrap()).unwrap();
        for (wire, song) in raw["data"]["songlist"]
            .as_array()
            .unwrap()
            .iter()
            .zip(result["songs"].as_array().unwrap())
        {
            let m = &song["metadata"];
            assert_eq!(m["audio_id"], wire["audio_id"]);
            assert_eq!(m["ranking"]["position"], wire["business"]["sort"]);
            assert_eq!(
                m["ranking"]["previous_position"],
                wire["business"]["last_sort"]
            );
            assert_eq!(m["ranking"]["rank_cid"], wire["rank_cid"]);
            assert_eq!(
                m["recommendation"]["headline"],
                wire["business"]["recommend_reason"]
            );
            assert_eq!(
                m["related_versions"].as_array().unwrap().len(),
                wire["remarks"].as_array().unwrap().len()
            );
            let standard = m["resources"]
                .as_array()
                .unwrap()
                .iter()
                .find(|r| r["quality"] == "128")
                .unwrap();
            assert_eq!(
                standard["privilege"],
                wire["privilege_download"]["privilege_128"]
            );
            assert_eq!(
                standard["download_status"],
                wire["user_download"]["status_128"]
            );
            assert_eq!(standard["payment_type"], wire["deprecated"]["pay_type_128"]);
        }
    }
    for name in ["home-default", "home-hires"] {
        let raw = raw("top_playlist", name);
        let result = serde_json::to_value(top_playlist::convert(&bytes(&raw)).unwrap()).unwrap();
        for (wire, card) in raw["data"]["special_list"]
            .as_array()
            .unwrap()
            .iter()
            .zip(result.as_array().unwrap())
        {
            assert_eq!(card["metadata"]["public_id"], wire["specialid"]);
            assert_eq!(card["metadata"]["creator_id"], wire["suid"]);
            assert_eq!(card["metadata"]["description"], wire["intro"]);
            assert_eq!(card["metadata"]["play_count"], wire["play_count"]);
            assert_eq!(
                card["metadata"]["collection_count"],
                wire.get("collectcount").cloned().unwrap_or(Value::Null)
            );
            assert!(card["song_count"].is_null());
        }
    }
}

#[test]
fn refreshed_current_style_capture_retains_live_shape() {
    let record: Value = serde_json::from_slice(include_bytes!(
        "../../../tests/fixtures/api/everyday_style_recommend/home-style-pages-live-20261016.json"
    ))
    .unwrap();
    let payload = &record["responses"].as_array().unwrap().last().unwrap()["payload"];
    for (index, song) in payload["data"]["song_list"]
        .as_array()
        .unwrap()
        .iter()
        .enumerate()
    {
        let _: echo_api::util::recommendation_song::Song = serde_json::from_value(song.clone())
            .unwrap_or_else(|error| panic!("live current style song {index}: {error}"));
    }
    everyday_style_recommend::convert(&bytes(payload)).unwrap();
}

#[test]
fn free_listen_keeps_every_quality_slot_card_and_metadata() {
    let record: Value = serde_json::from_slice(include_bytes!(
        "../../../tests/fixtures/api/ai_recommend_song/free-listen-explore-fields-20261016.json"
    ))
    .unwrap();
    let wire = &record["responses"][0]["payload"];
    let list = wire["data"]["list"].as_array().unwrap();
    assert_eq!(list.len(), 30);
    let result = ai_recommend_song::convert(&bytes(wire)).unwrap();
    assert_eq!(result.songs.len(), list.len());
    assert_eq!(result.card_info.as_ref().unwrap().card_id, Some(3107));
    let song = serde_json::to_value(&result.songs[0]).unwrap();
    assert_eq!(song["id"], "251721168");
    assert_eq!(song["title"], list[0]["songname"]);
    assert_eq!(
        song["hash"],
        list[0]["audio_info"]["hash"].as_str().unwrap()
    );
    assert_eq!(song["album_id"], "9618875");
    assert_eq!(song["artists"][0]["id"].as_u64(), Some(789204));
    assert_eq!(song["duration_ms"].as_u64(), Some(240927));
    let resources = song["metadata"]["resources"].as_array().unwrap();
    let row = |quality: &str| {
        resources
            .iter()
            .find(|r| r["quality"] == quality)
            .unwrap_or_else(|| panic!("missing quality {quality}"))
    };
    assert_eq!(row("128")["hash"], "7BCE0A72470D6466FBB0FCD111E9C8A4");
    assert_eq!(row("128")["size_bytes"].as_u64(), Some(3855453));
    assert_eq!(row("320")["hash"], "3D5A45EE70FB14AE7EA01F14656936CB");
    assert_eq!(row("flac")["hash"], "A99C0C8B0E297B2D51A3693E4FA30A50");
    assert_eq!(row("flac")["size_bytes"].as_u64(), Some(25840796));
    assert_eq!(row("flac")["bit_rate_bps"].as_u64(), Some(857000));
    assert_eq!(row("flac")["duration_ms"].as_u64(), Some(240000));
    assert_eq!(row("high")["hash"], "81E7CDD7ABB411C14E8D2FF3F130A73E");
    assert_eq!(row("high")["bit_rate_bps"].as_u64(), Some(910000));
    // OGG pairs come from trans_param, independently of the numbered slots.
    assert_eq!(row("ogg_128")["hash"], "47FDB340A993C89F4C84BEF523758449");
    assert_eq!(row("ogg_128")["size_bytes"].as_u64(), Some(2677118));
    assert_eq!(row("ogg_320")["size_bytes"].as_u64(), Some(9176984));
    assert_eq!(
        song["metadata"]["rights"]["quality_map"]["attr0"].as_u64(),
        Some(2134884469)
    );
    assert_eq!(
        song["metadata"]["rights"]["copyright_attributes"].as_u64(),
        Some(58752128)
    );
    assert_eq!(song["metadata"]["payment_type"].as_u64(), Some(3));
    assert_eq!(song["metadata"]["old_copyright_code"].as_u64(), Some(0));
    assert_eq!(song["metadata"]["failure_code"].as_u64(), Some(4));
    // cid has no established domain semantics yet: it stays verbatim in the
    // extension instead of being folded into another mapped identifier.
    let extension = &song["metadata"]["provider_extension"];
    assert_eq!(
        extension["audio_info"]["trans_param"]["cid"].as_u64(),
        Some(33159408)
    );
    assert!(extension["audio_info"]["is_file_head"].is_string());
    // remark semantics are unestablished: the raw key stays verbatim here and
    // is not mapped into album_audio_remark.
    assert_eq!(extension["remark"], list[0]["remark"]);
    assert_eq!(song["metadata"]["album_audio_remark"], "");
}

#[test]
fn home_discover_maps_song_items_with_album_fallback_and_verbatim_extras() {
    let record: Value = serde_json::from_slice(include_bytes!(
        "../../../tests/fixtures/api/home_discover/home-discover.json"
    ))
    .unwrap();
    let wire = &record["responses"][0]["payload"];
    let wire_items = wire["data"]["items"].as_array().unwrap();
    // The feed batch is not deterministic; the conversion is compared with
    // each capture itself instead of a pinned count.
    assert!(!wire_items.is_empty());
    let result = home_discover::convert(&bytes(wire)).unwrap();
    assert_eq!(result.items.len(), wire_items.len());
    assert!(result.unmapped_items.is_empty());
    let item = serde_json::to_value(&result.items[0]).unwrap();
    let first = &wire_items[0]["song_info"];
    // The mapping is compared against the capture itself: the recommendation
    // feed is not deterministic, so no cross-capture literal is asserted.
    assert_eq!(item["song"]["id"], first["mixsongid"]);
    // ori_album_id is the only album identity on this wire.
    assert_eq!(
        item["song"]["album_id"],
        first["ori_album_id"].as_u64().unwrap().to_string()
    );
    assert_eq!(item["song"]["hash"], first["hash_128"]);
    assert_eq!(
        item["song"]["duration_ms"].as_u64(),
        first["timelength_128"].as_u64()
    );
    assert_eq!(item["song"]["title"], first["songname"]);
    assert_eq!(item["song"]["artists"][0]["name"], first["author_name"]);
    let cover = item["song"]["cover_url"].as_str().unwrap();
    assert!(cover.starts_with("http://imge.kugou.com/stdmusic/240/"));
    let resources = item["song"]["metadata"]["resources"].as_array().unwrap();
    let row = |quality: &str| {
        resources
            .iter()
            .find(|r| r["quality"] == quality)
            .unwrap_or_else(|| panic!("missing quality {quality}"))
    };
    assert_eq!(row("128")["hash"], first["hash_128"]);
    assert_eq!(
        row("128")["size_bytes"].as_u64(),
        first["filesize_128"].as_u64()
    );
    assert_eq!(row("320")["hash"], first["hash_320"]);
    assert_eq!(
        row("320")["size_bytes"].as_u64(),
        first["filesize_320"].as_u64()
    );
    assert_eq!(row("flac")["hash"], first["hash_flac"]);
    assert_eq!(
        row("flac")["size_bytes"].as_u64(),
        first["filesize_flac"].as_u64()
    );
    assert_eq!(
        row("flac")["duration_ms"].as_u64(),
        first["timelength_flac"].as_u64()
    );
    let trans = &first["trans_param"];
    // The OGG pairs come from trans_param, independently of the numbered slots.
    assert_eq!(row("ogg_128")["hash"], trans["ogg_128_hash"]);
    assert_eq!(
        row("ogg_128")["size_bytes"].as_u64(),
        trans["ogg_128_filesize"].as_u64()
    );
    assert_eq!(row("ogg_320")["hash"], trans["ogg_320_hash"]);
    assert_eq!(
        row("ogg_320")["size_bytes"].as_u64(),
        trans["ogg_320_filesize"].as_u64()
    );
    assert_eq!(
        item["song"]["metadata"]["rights"]["originality_code"].as_u64(),
        trans["is_original"].as_u64()
    );
    assert_eq!(
        item["song"]["metadata"]["payment_type"].as_u64(),
        first["pay_type"].as_u64()
    );
    assert_eq!(
        item["song"]["metadata"]["failure_code"].as_u64(),
        first["fail_process"].as_u64()
    );
    assert_eq!(
        item["song"]["metadata"]["accompaniment_code"].as_u64(),
        first["has_obbligato"].as_u64()
    );
    // Unmodeled playback-proof data stays verbatim; no semantic guess is made.
    let extension = &item["song"]["metadata"]["provider_extension"];
    assert_eq!(extension["tracker_info"], first["tracker_info"]);
    assert_eq!(extension["songid"], first["songid"]);
    // The retained start-of-playback field keeps its wire value.
    assert_eq!(item["start_time"], wire_items[0]["extra"]["start_time"]);
    assert_eq!(item["item_id"], wire_items[0]["item_id"]);
    assert_eq!(item["alg_path"], wire_items[0]["alg_path"]);
}

#[test]
fn home_discover_device_only_capture_returns_rows_without_credentials() {
    let record: Value = serde_json::from_slice(include_bytes!(
        "../../../tests/fixtures/api/home_discover/home-discover-anonymous.json"
    ))
    .unwrap();
    assert_eq!(record["authenticated"], false);
    assert_eq!(
        record["credential_condition"],
        "device-only snapshot, no account credentials"
    );
    let request = &record["requests"][0];
    assert_eq!(request["params"]["userid"].as_u64(), Some(0));
    assert!(request["params"]["token"].is_null());
    assert_eq!(request["params"]["clientver"].as_u64(), Some(20809));
    assert_eq!(request["params"]["module_key"], "home_discover_rec");
    assert_eq!(request["body"]["userid"].as_u64(), Some(0));
    assert_eq!(request["body"]["pagesize"].as_u64(), Some(4));
    assert_eq!(request["body"]["support"], "only_song");
    assert_eq!(request["body"]["recall_type"], "song");
    let wire = &record["responses"][0]["payload"];
    let wire_items = wire["data"]["items"].as_array().unwrap();
    let result = home_discover::convert(&bytes(wire)).unwrap();
    assert_eq!(result.items.len(), wire_items.len());
    let first = serde_json::to_value(&result.items[0]).unwrap();
    let wire_first = &wire_items[0]["song_info"];
    assert_eq!(first["song"]["id"], wire_first["mixsongid"]);
    assert_eq!(first["song"]["hash"], wire_first["hash_128"]);
    assert_eq!(
        first["song"]["duration_ms"].as_u64(),
        wire_first["timelength_128"].as_u64()
    );
}
