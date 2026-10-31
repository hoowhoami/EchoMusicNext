use echo_api::util::{crypto, signature};
use std::collections::BTreeMap;
#[test]
fn tracker_rejection_keeps_only_safe_protocol_diagnostics() {
    // Failure-envelope scalars observed in the local recognize-source capture;
    // this checks error projection, not an unobserved success response.
    let bytes = br#"{"status":2,"error_code":"20010","msg":"private upstream text","data":{"auth":"secret"}}"#;
    let error = echo_api::module::song_url_auth::convert(bytes)
        .err()
        .unwrap();
    assert_eq!(error.kind, "business");
    assert_eq!(error.code, Some(20010));
    let value = serde_json::to_value(error).unwrap();
    assert_eq!(value["diagnostics"]["phase"], "tracker_response");
    assert_eq!(value["diagnostics"]["service_status"], 2);
    let encoded = serde_json::to_string(&value).unwrap();
    assert!(!encoded.contains("private upstream text"));
    assert!(!encoded.contains("secret"));
    let no_code = echo_api::module::song_url_auth::convert(br#"{"status":0}"#)
        .err()
        .unwrap();
    assert_eq!(no_code.code, None);
}
#[test]
fn automatic_tracker_quality_uses_reference_wire_default_and_preserves_origin() {
    use echo_api::module::{song_url_auth, song_url_auth_merge::Query};
    let query: Query = serde_json::from_value(serde_json::json!({
        "hash": "105A2705BEEE36A8BED029A8E1A6311D", "album_id": 960399,
        "song_id": 32042818, "quality": "automatic", "origin_page": "356753938"
    }))
    .unwrap();
    // server/module/song_url_auth.js: quality: quality || 128.
    assert_eq!(song_url_auth::tracker_quality(&query), "128");
    assert_eq!(query.origin_page, "356753938");
}
#[test]
fn user_authorization_uses_status_and_auth_without_an_extra_valid_gate() {
    use echo_api::module::user_verify;
    // Input boundary cases from api/music.ts and server/module/user_verify.js;
    // these are not claims of captured provider responses.
    for data in [
        serde_json::json!({"auth":"fixture-only","valid":0}),
        serde_json::json!({"auth":"fixture-only"}),
    ] {
        let response =
            serde_json::to_vec(&serde_json::json!({"status":1,"error_code":0,"data":data}))
                .unwrap();
        let auth = user_verify::convert(&response).unwrap();
        assert_eq!(auth.authorization, "fixture-only");
        assert!(!auth.valid);
    }
    for invalid in [
        serde_json::json!(""),
        serde_json::json!("  "),
        serde_json::json!(123),
        serde_json::json!({}),
    ] {
        let response = serde_json::to_vec(
            &serde_json::json!({"status":1,"error_code":0,"data":{"valid":1,"auth":invalid}}),
        )
        .unwrap();
        assert!(user_verify::convert(&response).is_err());
    }
    let denied = user_verify::convert(br#"{"status":0,"error_code":35005,"data":""}"#)
        .err()
        .unwrap();
    assert_eq!(denied.kind, "authentication");
}
#[test]
fn captured_tracker_effects_use_magic_protocol_names() {
    // Request protocol audited from server/module/song_url_auth.js; the six
    // genuine effects-20261013 fixtures separately establish response contracts.
    for (effect, expected) in [
        ("none", "flac"),
        ("piano", "magic_piano"),
        ("vocal", "magic_acappella"),
        ("accompaniment", "magic_acappella"),
        ("subwoofer", "magic_subwoofer"),
        ("ancient", "magic_ancient"),
        ("surnay", "magic_surnay"),
        ("dj", "magic_dj"),
    ] {
        let query = serde_json::from_value(serde_json::json!({
            "hash": "105A2705BEEE36A8BED029A8E1A6311D",
            "album_id": 960399, "song_id": 32042818,
            "quality": "flac", "effect": effect
        }))
        .unwrap();
        assert_eq!(
            echo_api::module::song_url_auth::tracker_quality(&query),
            expected
        );
    }
}
#[test]
fn cover_checksum_uses_the_host_calendar_day_and_validates_leap_dates() {
    use echo_api::module::playlist_pic_upload::checksum;
    assert_eq!(
        checksum("20261010").unwrap(),
        "021afcfed35175f9a266fe68ccda4c29"
    );
    assert!(checksum("20240229").is_ok());
    assert!(checksum("20000229").is_ok());
    for invalid in [
        "20260229",
        "19000229",
        "20261301",
        "20261000",
        "00000101",
        "2026101",
        "2026-10-10",
    ] {
        assert!(checksum(invalid).is_err());
    }
}
#[test]
fn signatures_match_legacy_protocol_vectors_for_text_and_binary() {
    assert_eq!(
        signature::tracker_key("0123456789abcdef0123456789abcdef", "fixture-device", 123),
        "8f19054f7ee833dcf695433c4c6815c9"
    );
    let p = BTreeMap::from([
        ("z".into(), "中文".into()),
        ("appid".into(), "3116".into()),
        ("clientver".into(), "201".into()),
        ("mid".into(), "fixture-device".into()),
    ]);
    assert_eq!(
        signature::android(&p, "{\"query\":\"音乐\"}".as_bytes()),
        "d3bf3b33b8232ccd958207b21ec37a2b"
    );
    let p = BTreeMap::from([
        ("hash".into(), "FIXTURE".into()),
        ("userid".into(), "123".into()),
        ("appid".into(), "3116".into()),
    ]);
    assert_eq!(
        signature::android(&p, &[0, 255, 128, 32]),
        "309f8f751b9c940b11395db98934faf8"
    );
}
#[test]
fn raw_rsa_matches_right_padding_and_fixed_width_legacy_vector() {
    let plain = b"{\"token\":\"fixture-token\",\"clienttime\":1700000000}";
    assert_eq!(
        crypto::user_portrait(plain).unwrap(),
        "391DA68766706121FCEF98FC5B285AAB4F78499BDE78FE1FADBF48D38B80C7D1F07481588D604727BC8CF4ADA90237BDD153C19E4402C686EB23A4FA89F33259AE192426B5DDDB0D95EFA43C0B28B4EEAE84C1026B257900F685F5BA7E9A1F44279FA63D7227B82AEB60E4EB67700DD5210E5DD5C8EC6BA7A6CC4C5ECB286475"
    );
    assert!(crypto::user_portrait(&[0; 129]).is_err());
}

#[test]
fn h5_signing_matches_legacy_vector_with_url_parameters() {
    let params = std::collections::BTreeMap::from([
        ("appid".into(), "1001".into()),
        ("clienttime".into(), "1700000000".into()),
        ("qrcode_txt".into(), "https://example.invalid/?a=1&".into()),
    ]);
    assert_eq!(
        echo_api::util::signature::web(&params, b""),
        "1e8faf77fa53393477ae940efe2e17f1"
    );
}

#[test]
fn avatar_transport_and_account_submission_are_distinct_requests() {
    use echo_api::Request;
    let identity = serde_json::json!({"userid":17,"token":"test","mid":"test","dfid":"test"});
    let upload = serde_json::json!({"operation":"user_avatar_upload","identity":identity,"query":{"image_base64":"AA==","local_date":"20261023"}});
    assert!(matches!(
        serde_json::from_value::<Request>(upload).unwrap(),
        Request::UserAvatarUpload { .. }
    ));
    let update = serde_json::json!({"operation":"user_update_avatar","identity":identity,"query":{"file_name":"sample-cover.jpg"}});
    assert!(matches!(
        serde_json::from_value::<Request>(update).unwrap(),
        Request::UserUpdateAvatar { .. }
    ));
    let combined = serde_json::json!({"operation":"user_update_avatar","identity":identity,"query":{"image_base64":"AA==","local_date":"20261023"}});
    assert!(serde_json::from_value::<Request>(combined).is_err());
}
