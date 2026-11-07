use echo_api::module;
use serde_json::Value;
fn payload(module: &str, name: &str) -> Vec<u8> {
    let p = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/fixtures/api")
        .join(module)
        .join(format!("{name}.json"));
    let v: Value = serde_json::from_slice(&std::fs::read(p).unwrap()).unwrap();
    serde_json::to_vec(&v["responses"][0]["payload"]).unwrap()
}
#[test]
fn collected_playlist_and_album_acknowledge_local_library_ids() {
    for (name, id, previous, version) in [("playlist", 122, 3215, 3216), ("album", 123, 3217, 3218)]
    {
        let ack = module::playlist_add::convert(&payload(
            "playlist_add",
            &format!("social-actions-{name}-add-20261019"),
        ))
        .unwrap();
        assert_eq!(ack.cloud_list_id, Some(id));
        assert_eq!(ack.previous_total_revision, Some(previous));
        assert_eq!(ack.total_revision, Some(version));
        assert_eq!(ack.playlist_count, Some(47));
        let removed = module::playlist_del::convert(
            &payload(
                "playlist_del",
                &format!("social-actions-{name}-remove-20261019"),
            ),
            id,
        )
        .unwrap();
        assert_eq!(removed.cloud_list_id, Some(id));
        assert_eq!(removed.playlist_count, Some(46));
    }
}
#[test]
fn sampled_artist_and_social_mutations() {
    assert_eq!(
        module::artist_follow::convert(&payload("artist_follow", "social-actions-follow-20261019"))
            .unwrap()["following"],
        true
    );
    assert_eq!(
        module::artist_unfollow::convert(&payload(
            "artist_unfollow",
            "social-actions-unfollow-20261019"
        ))
        .unwrap()["following"],
        false
    );
    assert!(
        !module::user_follow_del::convert(&payload("user_follow_del", "restore-original"))
            .unwrap()
            .following
    );
    assert!(
        module::user_follow_add::convert(&payload("user_follow_add", "attempt-mutual")).is_err()
    );
}
#[test]
fn current_private_history_maps_sampled_text_sender_and_conversation() {
    let bytes = payload("user_follow_message", "next-current-2-20261103");
    let history = module::user_follow_message::convert(&bytes).unwrap();
    assert_eq!(history.records.len(), 1);
    let row = &history.records[0];
    assert_eq!(row.id, "900004");
    assert_eq!(row.sender_id, 900001);
    assert_eq!(row.sent_at_seconds, Some(1790607092));
    assert_eq!(row.text, "[redacted]");
    assert_eq!(history.tag.as_deref(), Some("chat:900003_900001"));
    assert!(history.unmapped_records.is_empty());
    assert_eq!(history.ended, Some(true));
    for n in [1, 3, 4, 5] {
        let empty = module::user_follow_message::convert(&payload(
            "user_follow_message",
            &format!("next-current-{n}-20261103"),
        ))
        .unwrap();
        assert!(empty.records.is_empty());
    }
    // Controlled mutations test tolerance; these are not provider samples.
    let mut raw: Value = serde_json::from_slice(&bytes).unwrap();
    let mut unknown = raw["list"][0].clone();
    unknown["message"]["msgtype"] = 202.into();
    raw["list"].as_array_mut().unwrap().push(unknown);
    let mixed = module::user_follow_message::convert(&serde_json::to_vec(&raw).unwrap()).unwrap();
    assert_eq!(mixed.records.len(), 1);
    assert_eq!(mixed.unmapped_records.len(), 1);
}

#[test]
fn acknowledged_outgoing_messages_live_under_the_other_tag_order() {
    let empty = module::user_follow_message::convert(&payload(
        "user_follow_message",
        "next-current-1-ci-matrix-20261103-reversefalse-filter1",
    ))
    .unwrap();
    let outgoing = module::user_follow_message::convert(&payload(
        "user_follow_message",
        "next-current-1-ci-matrix-20261103-reversetrue-filter1",
    ))
    .unwrap();
    assert!(empty.records.is_empty());
    assert_eq!(outgoing.records.len(), 6);
    assert_eq!(outgoing.tag.as_deref(), Some("chat:900001_900002"));
    assert!(outgoing.records.iter().all(|row| row.sender_id == 900001));
    assert_eq!(
        outgoing.records.last().unwrap().sent_at_seconds,
        Some(1793685306)
    );
}

#[test]
fn real_empty_histories_and_message_ack() {
    for n in 1..=12 {
        let page = module::user_follow_message::convert(&payload(
            "user_follow_message",
            &format!("social-actions-existing-{n}-20261019"),
        ))
        .unwrap();
        assert!(page.records.is_empty());
        assert_eq!(page.ended, Some(true));
    }
    let ack = module::user_follow_chat::convert(&payload("user_follow_chat", "target")).unwrap();
    assert!(!ack.msgid.is_empty());
    assert!(!ack.tag.is_empty());
    assert_eq!(ack.addtime, Some(1792242093));
    let rejected = module::user_follow_chat::convert(&payload("user_follow_chat", "self"))
        .err()
        .unwrap();
    assert_eq!(rejected.code, Some(2001));
}
