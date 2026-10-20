#![no_main]
use echo_api::util::response;
use libfuzzer_sys::fuzz_target;
use serde_json::Value;

fuzz_target!(|bytes: &[u8]| {
    let _ = serde_json::from_slice::<response::Envelope<Value>>(bytes);
    let _ = serde_json::from_slice::<response::DefaultDataEnvelope<Value>>(bytes);
    let _ = serde_json::from_slice::<response::AckEnvelope>(bytes);
    let _ = serde_json::from_slice::<response::ErrcodeEnvelope<Value>>(bytes);
    if let Ok(value) = serde_json::from_slice::<Value>(bytes) {
        let _ = response::from_value::<u64>(value.clone());
        let _ = response::from_value::<i64>(value.clone());
        let _ = response::from_value::<Vec<Value>>(value);
    }
    let _ = echo_api::module::search_hot::convert(bytes);
    let _ = echo_api::module::login_token::convert(bytes, &[], 0);
});
