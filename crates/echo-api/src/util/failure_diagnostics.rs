//! Request-scoped diagnostics: success drops the bounded capture; failure sends
//! redacted wire/decoded sections to the host alongside its logical request ID.
use crate::error::ApiError;
use serde_json::{Value, json};
use std::{cell::RefCell, collections::VecDeque, future::Future};

const BODY_LIMIT: usize = 32 * 1024;
const EXCHANGE_LIMIT: usize = 4;
#[derive(Default)]
struct Exchange {
    request: Value,
    response: Option<(u16, Value, Vec<u8>, usize)>,
    decoded: Option<(Vec<u8>, usize)>,
    sensitive_handshake: bool,
}
#[derive(Default)]
struct Capture {
    exchanges: VecDeque<Exchange>,
    secrets: Vec<String>,
    omitted: usize,
}
tokio::task_local! {
    static CAPTURE: RefCell<Capture>;
}
fn body(bytes: &[u8]) -> Value {
    match serde_json::from_slice(bytes) {
        Ok(value) => value,
        Err(_) => match std::str::from_utf8(bytes) {
            Ok(text) if !text.contains('\0') => Value::String(text.to_owned()),
            _ => json!({"format":"binary", "captured_bytes":bytes.len()}),
        },
    }
}
fn collect_secrets(value: &Value, secrets: &mut Vec<String>) {
    match value {
        Value::Object(object) => {
            for (key, value) in object {
                if super::response_logging::sensitive(key) {
                    if let Some(text) = value.as_str()
                        && (4..=4096).contains(&text.len())
                        && secrets.len() < 128
                    {
                        secrets.push(text.to_owned());
                    }
                } else {
                    collect_secrets(value, secrets);
                }
            }
        }
        Value::Array(values) => {
            for value in values.iter().take(128) {
                collect_secrets(value, secrets);
            }
        }
        _ => {}
    }
}
fn headers(value: &reqwest::header::HeaderMap) -> Value {
    Value::Object(
        value
            .iter()
            .map(|(key, value)| {
                (
                    key.to_string(),
                    Value::String(value.to_str().unwrap_or("[binary]").to_owned()),
                )
            })
            .collect(),
    )
}
pub fn request(request: &reqwest::Request) {
    let _ = CAPTURE.try_with(|capture| {
        let mut capture = capture.borrow_mut();
        let path = request.url().path();
        let query: std::collections::BTreeMap<_, _> =
            request.url().query_pairs().into_owned().collect();
        let raw_body = request
            .body()
            .and_then(reqwest::Body::as_bytes)
            .unwrap_or_default();
        let value = json!({
            "method": request.method().as_str(), "host":request.url().host_str(),
            "path":path, "query":query, "headers":headers(request.headers()),
            "body": body(&raw_body[..raw_body.len().min(BODY_LIMIT)]),
            "body_bytes":raw_body.len(), "body_truncated":raw_body.len() > BODY_LIMIT
        });
        collect_secrets(&value, &mut capture.secrets);
        let sensitive_handshake = matches!(
            path,
            "/v2/gen"
                | "/v3/qrydid"
                | "/bsstrackercdngz/v1/upload/auth"
                | "/v2/multipart/initiate/music"
                | "/v3/multipart/upload"
                | "/v3/multipart/complete"
        );
        if capture.exchanges.len() == EXCHANGE_LIMIT {
            capture.exchanges.pop_front();
            capture.omitted += 1;
        }
        let request = super::response_logging::diagnostic_value(&value, &capture.secrets);
        capture.exchanges.push_back(Exchange {
            request,
            sensitive_handshake,
            ..Exchange::default()
        });
    });
}
pub fn forwarded_request(forwarded: &reqwest::Request, intercepted: bool) {
    let _ = CAPTURE.try_with(|capture| {
        capture.borrow_mut().exchanges.pop_back();
    });
    request(forwarded);
    if intercepted {
        intercepted_response();
    }
}
pub fn intercepted_response() {
    let _ = CAPTURE.try_with(|capture| {
        if let Some(exchange) = capture.borrow_mut().exchanges.back_mut() {
            exchange.request["origin"] = json!("plugin-interceptor");
        }
    });
}
pub fn response(status: u16, response_headers: &reqwest::header::HeaderMap, bytes: &[u8]) {
    let _ = CAPTURE.try_with(|capture| {
        if let Some(exchange) = capture.borrow_mut().exchanges.back_mut() {
            exchange.response = Some((
                status,
                headers(response_headers),
                bytes[..bytes.len().min(BODY_LIMIT)].to_vec(),
                bytes.len(),
            ));
        }
    });
}
pub fn decoded(bytes: &[u8]) {
    let _ = CAPTURE.try_with(|capture| {
        if let Some(exchange) = capture.borrow_mut().exchanges.back_mut()
            && !exchange.sensitive_handshake
            && exchange
                .response
                .as_ref()
                .is_none_or(|(_, _, wire, _)| wire != bytes)
        {
            exchange.decoded = Some((bytes[..bytes.len().min(BODY_LIMIT)].to_vec(), bytes.len()));
        }
    });
}
fn sections(capture: &Capture) -> Vec<Value> {
    let mut records = Vec::new();
    for (index, exchange) in capture.exchanges.iter().enumerate() {
        let mut request = exchange.request.clone();
        if exchange.sensitive_handshake {
            request["body"] = json!("[redacted sensitive handshake]");
        }
        let mut values = vec![("request", request)];
        if let Some((status, metadata, bytes, size)) = &exchange.response {
            values.push(("response", json!({"http_status":status,"headers":metadata,"body_bytes":size,"body_truncated":*size > BODY_LIMIT,
                "body":if exchange.sensitive_handshake {json!("[redacted sensitive handshake]")} else {body(bytes)}
            })));
        }
        if let Some((bytes, size)) = &exchange.decoded {
            values.push((
                "decoded_response",
                json!({"body":body(bytes),
                "body_bytes":size,"body_truncated":*size > BODY_LIMIT}),
            ));
        }
        for (stage, value) in values {
            records.push(json!({"exchange":index + capture.omitted + 1,"stage":stage,
                "content":super::response_logging::diagnostic_value(&value, &capture.secrets)}));
        }
    }
    records
}
pub async fn capture<F, T>(future: F) -> Result<T, ApiError>
where
    F: Future<Output = Result<T, ApiError>>,
{
    CAPTURE
        .scope(RefCell::new(Capture::default()), async {
            let mut result = future.await;
            if let Err(error) = &mut result {
                CAPTURE.with(|capture| {
                    let capture = capture.borrow();
                    // Preserve the service's own business explanation. Only named
                    // envelope message fields are product text, never raw HTML,
                    // transport errors, credentials or arbitrary nested item data.
                    if error.kind == "business"
                        && let Some(exchange) = capture.exchanges.back()
                        && !exchange.sensitive_handshake
                    {
                        let bytes = exchange
                            .decoded
                            .as_ref()
                            .map(|(bytes, _)| bytes)
                            .or_else(|| exchange.response.as_ref().map(|(_, _, bytes, _)| bytes));
                        if let Some(value) =
                            bytes.and_then(|bytes| serde_json::from_slice::<Value>(bytes).ok())
                        {
                            for key in ["message", "msg", "errmsg", "error_msg"] {
                                if let Some(message) =
                                    value.get(key).and_then(Value::as_str).map(str::trim)
                                    && !message.is_empty()
                                {
                                    let safe = super::response_logging::diagnostic_value(
                                        &Value::String(message.chars().take(512).collect()),
                                        &capture.secrets,
                                    );
                                    if let Some(message) = safe.as_str() {
                                        error.message = message.to_owned();
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    // A parallel subrequest may already carry its own isolated
                    // capture. Keep it when the outer logical scope unwinds.
                    error.response_details.extend(sections(&capture));
                });
            }
            result
        })
        .await
}
#[cfg(test)]
mod tests {
    use super::*;
    #[tokio::test]
    async fn failures_keep_non_json_and_decoded_bodies_without_request_secrets() {
        let request = reqwest::Client::new()
            .post("http://localhost/sample?token=request-secret&page=2")
            .header("authorization", "Bearer header-secret")
            .json(&json!({"password":"body-secret", "title":"ordinary song"}))
            .build()
            .unwrap();
        let error = capture::<_, Vec<u8>>(async {
            super::request(&request);
            response(200, &reqwest::header::HeaderMap::new(), b"upstream broke: request-secret token=response-secret");
            decoded(br#"{"status":0,"error_code":206,"message":"useful decoded detail","token":"decoded-secret"}"#);
            Err(ApiError::business(206))
        }).await.unwrap_err();
        let logged = serde_json::to_string(&error.response_details).unwrap();
        assert_eq!(error.message, "useful decoded detail");
        assert!(logged.contains("upstream broke"));
        assert!(logged.contains("useful decoded detail"));
        assert!(logged.contains("ordinary song"));
        for secret in [
            "request-secret",
            "header-secret",
            "body-secret",
            "response-secret",
            "decoded-secret",
        ] {
            assert!(!logged.contains(secret), "leaked {secret}");
        }
        for record in error.response_details {
            assert!(record.to_string().len() < 7500);
        }
    }
    #[tokio::test]
    async fn captures_are_request_local_and_success_does_not_return_diagnostics() {
        let one = capture(async {
            decoded(b"other request");
            Ok(vec![1])
        });
        let two = capture::<_, Vec<u8>>(async { Err(ApiError::decode()) });
        let (one, two) = tokio::join!(one, two);
        assert_eq!(one.unwrap(), vec![1]);
        assert!(two.unwrap_err().response_details.is_empty());
    }
    #[tokio::test]
    async fn parallel_subrequests_keep_their_wire_pairing_when_the_parent_fails() {
        async fn failed(label: &str) -> Result<(), ApiError> {
            capture(async {
                let request = reqwest::Client::new()
                    .get(format!("http://localhost/{label}"))
                    .build()
                    .unwrap();
                super::request(&request);
                tokio::task::yield_now().await;
                response(
                    200,
                    &reqwest::header::HeaderMap::new(),
                    format!("{label} response").as_bytes(),
                );
                Err(ApiError::business(206))
            })
            .await
        }
        let (left, right) = tokio::join!(failed("left"), failed("right"));
        for (error, own, other) in [
            (left.unwrap_err(), "left", "right"),
            (right.unwrap_err(), "right", "left"),
        ] {
            let details = serde_json::to_string(&error.response_details).unwrap();
            assert!(details.contains(&format!("/{own}")));
            assert!(details.contains(&format!("{own} response")));
            assert!(!details.contains(other));
        }
        let nested = capture(failed("nested")).await.unwrap_err();
        assert_eq!(nested.response_details.len(), 2);
    }
}
