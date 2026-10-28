//! Optional response diagnostics, independent of response DTO/conversion rules.
//! Secrets must be removed before entering either the Rust or Qt log queue.
use serde_json::{Map, Value};
use std::sync::atomic::{AtomicBool, Ordering};

static ENABLED: AtomicBool = AtomicBool::new(false);
pub fn set_enabled(enabled: bool) {
    ENABLED.store(enabled, Ordering::Relaxed);
}
fn sensitive(key: &str) -> bool {
    let key = key.to_ascii_lowercase();
    [
        "token",
        "password",
        "passwd",
        "secret",
        "cookie",
        "authorization",
        "ticket",
        "randstr",
        "proof",
        "secu",
        "ssecode",
        "qrsig",
        "access_key",
        "signature",
        "mobile",
        "phone",
        "email",
        "key",
        "pwd",
        "verifycode",
        "qrcode",
        "login_sig",
        "h5sig",
        "fingerprint",
        "dev",
        "username",
        "dfid",
        "mid",
        "uuid",
        "guid",
        "mac",
    ]
    .iter()
    .any(|part| key.contains(part))
        || matches!(
            key.as_str(),
            "t1" | "mid" | "dfid" | "uuid" | "guid" | "mac" | "sid" | "edt" | "dev" | "serverdev"
        )
}
fn sanitize(value: &Value, depth: usize, budget: &mut usize) -> Value {
    if depth >= 6 || *budget < 24 {
        return Value::String("[truncated]".into());
    }
    *budget -= 24;
    match value {
        Value::Object(object) => {
            let mut result = Map::new();
            for (key, value) in object.iter().take(32) {
                if *budget < 24 {
                    break;
                }
                // Keys are also untrusted response data. Bound them and refuse
                // labels that themselves contain a URL or an oversized value.
                let label: String = key.chars().take(64).collect();
                *budget = budget.saturating_sub(label.len());
                result.insert(
                    label,
                    if sensitive(key) {
                        Value::String("[redacted]".into())
                    } else {
                        sanitize(value, depth + 1, budget)
                    },
                );
            }
            if result.len() < object.len() {
                result.insert("_truncated".into(), Value::Bool(true));
            }
            Value::Object(result)
        }
        Value::Array(array) => {
            let mut result = Vec::new();
            for value in array.iter().take(16) {
                if *budget < 24 {
                    break;
                }
                result.push(sanitize(value, depth + 1, budget));
            }
            Value::Array(result)
        }
        Value::String(text) => {
            if text.contains("://") || text.to_ascii_lowercase().starts_with("bearer ") {
                return Value::String("[redacted address or credential]".into());
            }
            let limit = (*budget).min(256);
            let clipped: String = text.chars().take(limit / 4).collect();
            *budget = budget.saturating_sub(clipped.len());
            Value::String(if clipped.len() < text.len() {
                clipped + "…"
            } else {
                clipped
            })
        }
        scalar => scalar.clone(),
    }
}
fn payload(bytes: &[u8], status: u16, path: &str) -> String {
    let mut budget = 3500;
    let response = serde_json::from_slice::<Value>(bytes)
        .map(|body| sanitize(&body, 0, &mut budget))
        .unwrap_or_else(|_| Value::String("[non-JSON response omitted]".into()));
    // Only the static path (not query, host, headers or request body) is used.
    serde_json::json!({"httpStatus": status, "path": path, "response": response}).to_string()
}
pub fn record(bytes: &[u8], status: u16, path: &str) {
    record_origin(bytes, status, path, false);
}
pub fn record_intercepted(bytes: &[u8], status: u16, path: &str) {
    record_origin(bytes, status, path, true);
}
fn record_origin(bytes: &[u8], status: u16, path: &str, intercepted: bool) {
    let level = if status >= 400 {
        1
    } else if ENABLED.load(Ordering::Relaxed) {
        2
    } else {
        4
    };
    if echo_log::enabled(level) {
        let mut value = payload(bytes, status, path);
        if intercepted {
            // Intercepted/mock bytes are never presented as an untouched provider sample.
            if let Ok(mut record) = serde_json::from_str::<Value>(&value) {
                record["origin"] = Value::String("plugin-interceptor".into());
                value = record.to_string();
            }
        }
        echo_log::write(level, "native.api.response", value);
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn redacts_before_queue_and_keeps_non_secret_diagnostics() {
        let logged = payload(br#"{"status":1,"nested":{"token":"do-not-store","t1":"refresh-secret","dfid":"device-secret","url":"https://signed.invalid/a?key=secret","name":"ordinary song"},"invalid":null}"#, 200, "/sample");
        assert!(logged.contains("ordinary song"));
        for secret in [
            "do-not-store",
            "refresh-secret",
            "device-secret",
            "signed.invalid",
            "key=secret",
        ] {
            assert!(!logged.contains(secret));
        }
        assert!(serde_json::from_str::<Value>(&logged).is_ok());
        assert!(!payload(b"not-json-secret", 503, "/sample").contains("not-json-secret"));
    }
    #[test]
    fn bounds_large_nested_values() {
        let logged = payload(
            &serde_json::to_vec(&serde_json::json!({"entries":vec!["x".repeat(10000);1000]}))
                .unwrap(),
            200,
            "/sample",
        );
        assert!(logged.len() < 8192);
        assert!(serde_json::from_str::<Value>(&logged).is_ok());
    }
}
