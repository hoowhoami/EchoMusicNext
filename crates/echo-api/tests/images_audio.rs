use echo_api::module::images_audio::{self, Query};
use serde_json::json;

#[test]
fn images_audio_preserves_signed_json_order_and_does_not_add_identity_defaults() {
    let query: Query = serde_json::from_value(json!({"resources":[{
        "hash":"105A2705BEEE36A8BED029A8E1A6311D", "album_audio_id":32042818,"filename":"晴天"
    }]}))
    .unwrap();
    let params = images_audio::parameters(&query).unwrap();
    assert_eq!(
        params["data"],
        r#"[{"audio_id":0,"hash":"105A2705BEEE36A8BED029A8E1A6311D","album_audio_id":"32042818","filename":"晴天"}]"#
    );
    assert_eq!(params.len(), 7);
    // Golden vector from the source request builder, not a response fixture.
    assert_eq!(
        echo_api::util::signature::android(&params, &[]),
        "c428aa7c45c7ca1f17b21f20f0a14f19"
    );
    for invalid in [
        json!({"resources":[]}),
        json!({"resources":[{"hash":"bad"}]}),
        json!({"resources":[{"hash":"00000000000000000000000000000000"}],"count":0}),
        json!({"resources":[{"hash":"00000000000000000000000000000000"}],"count":51}),
    ] {
        let query: Query = serde_json::from_value(invalid).unwrap();
        assert_eq!(images_audio::parameters(&query).unwrap_err().kind, "input");
    }
}

#[test]
fn images_audio_errors_are_distinct_from_empty_success() {
    let empty = images_audio::convert(br#"{"status":1,"error_code":0,"data":[[],[]]}"#).unwrap();
    assert_eq!(empty.len(), 2);
    assert!(empty.iter().all(Vec::is_empty));
    let error = images_audio::convert(br#"{"status":0,"error_code":20010}"#).unwrap_err();
    assert_eq!(error.code, Some(20010));
    assert!(images_audio::convert(b"invalid JSON").is_err());
}

#[cfg(feature = "local-api-fixtures")]
#[test]
fn images_audio_real_groups_categories_and_optional_fields() {
    fn sample(name: &str) -> serde_json::Value {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(format!(
            "../../tests/fixtures/api/images_audio/{name}-20261028.json"
        ));
        let record: serde_json::Value =
            serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap();
        record["responses"][0]["payload"].clone()
    }
    let raw = sample("portraits");
    let groups = images_audio::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
    let artist = &groups[0][0];
    assert_eq!(artist.artist_id.as_deref(), Some("3520"));
    assert_eq!(artist.name.as_deref(), Some("周杰伦"));
    assert!(artist.avatar_url.as_ref().unwrap().contains("/{size}/"));
    assert_eq!(artist.images["3"][0].id.as_deref(), Some("5548217"));
    assert!(
        artist.images["3"][0]
            .url
            .as_ref()
            .unwrap()
            .starts_with("https://")
    );
    for (kind, portraits) in raw["data"][0][0]["imgs"].as_object().unwrap() {
        assert_eq!(
            artist.images[kind].len(),
            portraits.as_array().unwrap().len()
        );
    }
    let mut partial = raw.clone();
    partial["data"][0][0]["author_id"] = json!("9007199254740993");
    partial["data"][0][0]["imgs"]["future"] = json!(null);
    partial["data"][0][0]["imgs"]["3"][0]["source"] = json!({"unknown":true});
    partial["data"][0][0]["imgs"]["3"][0]["sizable_portrait"] = json!("file:///private/image");
    let converted = images_audio::convert(&serde_json::to_vec(&partial).unwrap()).unwrap();
    assert_eq!(
        converted[0][0].artist_id.as_deref(),
        Some("9007199254740993")
    );
    assert!(converted[0][0].images["future"].is_empty());
    assert!(converted[0][0].images["3"][0].source.is_none());
    assert!(converted[0][0].images["3"][0].url.is_none());
    assert_eq!(converted[0][0].images["2"].len(), artist.images["2"].len());
    let batch = images_audio::convert(&serde_json::to_vec(&sample("batch")).unwrap()).unwrap();
    assert_eq!(batch.len(), 2);
    assert_eq!(batch[0].len(), 1);
    assert!(batch[0][0].images.is_empty());
    assert!(batch[1].is_empty());
    let empty = images_audio::convert(&serde_json::to_vec(&sample("empty")).unwrap()).unwrap();
    assert_eq!(empty.len(), 1);
    assert!(empty[0].is_empty());
}
