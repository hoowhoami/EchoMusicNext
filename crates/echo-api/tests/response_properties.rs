use echo_api::util::{request, response};
use proptest::prelude::*;
use serde::Deserialize;
use serde_json::{Value, json};

#[derive(Deserialize)]
struct OptionalFact {
    #[serde(default, deserialize_with = "response::optional")]
    value: Option<u64>,
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(512))]

    #[test]
    fn identifiers_never_round_through_float(value: u64) {
        let spelling = value.to_string();
        prop_assert_eq!(response::from_value::<u64>(Value::String(spelling)).unwrap(), value);
        prop_assert_eq!(response::from_value::<u64>(json!(value)).unwrap(), value);
    }

    #[test]
    fn signed_numeric_strings_preserve_boundaries(value: i64) {
        prop_assert_eq!(response::from_value::<i64>(Value::String(value.to_string())).unwrap(), value);
    }

    #[test]
    fn overflow_is_an_error_instead_of_truncation(extra in 1_u64..=u64::MAX) {
        let value = u64::MAX as u128 + extra as u128;
        prop_assert!(response::from_value::<u64>(Value::String(value.to_string())).is_err());
    }

    #[test]
    fn acknowledgement_requires_both_exact_success_fields(status: u32, code: i64) {
        prop_assert_eq!(request::success(status, code).is_ok(), status == 1 && code == 0);
    }

    #[test]
    fn unknown_optional_fact_stays_unknown(text in "[A-Za-z]{1,80}") {
        let fact: OptionalFact = serde_json::from_value(json!({"value": text})).unwrap();
        prop_assert_eq!(fact.value, None);
    }

    #[test]
    fn bounded_untrusted_bytes_cannot_panic(bytes in prop::collection::vec(any::<u8>(), 0..8192)) {
        let _ = echo_api::decode_ffi_request(&bytes);
        let _ = serde_json::from_slice::<response::Envelope<Value>>(&bytes);
    }
}

#[test]
fn envelope_shapes_keep_required_data_and_error_keys_distinct() {
    let valid = json!({"status": "1", "error_code": "0", "data": null});
    let wire: response::Envelope<Value> = serde_json::from_value(valid).unwrap();
    assert_eq!(
        (wire.status, wire.error_code, wire.data),
        (1, 0, Value::Null)
    );
    let no_data = json!({"status": 1, "error_code": 0});
    assert!(serde_json::from_value::<response::Envelope<Value>>(no_data.clone()).is_err());
    assert_eq!(
        serde_json::from_value::<response::DefaultDataEnvelope<Value>>(no_data.clone())
            .unwrap()
            .data,
        Value::Null
    );
    assert!(serde_json::from_value::<response::AckEnvelope>(no_data).is_ok());
    assert!(
        serde_json::from_value::<response::Envelope<Value>>(
            json!({"status":1,"errcode":0,"data":{}})
        )
        .is_err()
    );
    assert!(
        serde_json::from_value::<response::ErrcodeEnvelope<Value>>(
            json!({"status":1,"error_code":0,"data":{}})
        )
        .is_err()
    );
    for proof in [json!(null), json!("unknown"), json!({}), json!(false)] {
        assert!(
            serde_json::from_value::<response::AckEnvelope>(
                json!({"status": 1, "error_code":proof})
            )
            .is_err()
        );
    }
}

#[test]
fn ffi_ingress_rejects_oversized_empty_and_unknown_operations() {
    assert!(echo_api::decode_ffi_request(&[]).is_none());
    assert!(echo_api::decode_ffi_request(&vec![b' '; 1024 * 1024 + 1]).is_none());
    assert!(echo_api::decode_ffi_request(br#"{"operation":"not_an_operation"}"#).is_none());
    assert!(echo_api::decode_ffi_request(br#"{"operation":"capabilities"}"#).is_some());
}
