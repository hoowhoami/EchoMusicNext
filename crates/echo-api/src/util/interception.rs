//! Optional host transport middleware. No plugin registry or business state lives
//! here: each task borrows a host exchange, and dropping the future cancels it.
use crate::error::ApiError;
use base64::{Engine, engine::general_purpose::STANDARD};
use reqwest::{
    Request,
    header::{HeaderMap, HeaderName, HeaderValue},
};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, future::Future, pin::Pin, sync::Arc};
use tokio::sync::mpsc;

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct WireResponse {
    pub status: u16,
    pub headers: BTreeMap<String, Vec<String>>,
    pub body_base64: String,
}
pub struct Exchange {
    pub replies: mpsc::UnboundedReceiver<serde_json::Value>,
    pub notify: Box<dyn Fn(u32, serde_json::Value) + Send + Sync>,
    // FFI supplies a guard that removes the pending exchange on cancellation.
    pub _guard: Box<dyn Send + Sync>,
}
pub type ExchangeFuture = Pin<Box<dyn Future<Output = Result<Exchange, ApiError>> + Send>>;
pub struct Hook {
    pub operation: String,
    pub open: Box<dyn Fn(serde_json::Value) -> ExchangeFuture + Send + Sync>,
}
tokio::task_local! {pub static HOST_HOOK: Arc<Hook>;}
pub fn headers(map: &HeaderMap) -> BTreeMap<String, Vec<String>> {
    map.keys()
        .map(|key| {
            (
                key.to_string(),
                map.get_all(key)
                    .iter()
                    .map(|v| String::from_utf8_lossy(v.as_bytes()).into_owned())
                    .collect(),
            )
        })
        .collect()
}
fn decode_body(text: &str) -> Result<Vec<u8>, ApiError> {
    if text.len() > 24 * 1024 * 1024 {
        return Err(ApiError::new("limit", "拦截响应过大"));
    }
    let bytes = STANDARD
        .decode(text)
        .map_err(|_| ApiError::new("input", "无效拦截数据"))?;
    if bytes.len() > 16 * 1024 * 1024 {
        return Err(ApiError::new("limit", "拦截数据过大"));
    }
    Ok(bytes)
}
fn decode_headers(value: &serde_json::Value) -> Result<HeaderMap, ApiError> {
    let values: BTreeMap<String, Vec<String>> = serde_json::from_value(value.clone())
        .map_err(|_| ApiError::new("input", "无效拦截请求头"))?;
    if values.len() > 128 {
        return Err(ApiError::new("limit", "拦截请求头过多"));
    }
    let mut result = HeaderMap::new();
    let mut budget = 0usize;
    for (key, items) in values {
        let name = HeaderName::from_bytes(key.as_bytes())
            .map_err(|_| ApiError::new("input", "无效请求头"))?;
        for item in items {
            budget = budget.saturating_add(key.len()).saturating_add(item.len());
            if budget > 128 * 1024 || result.len() >= 512 {
                return Err(ApiError::new("limit", "拦截请求头过大"));
            }
            let value =
                HeaderValue::from_str(&item).map_err(|_| ApiError::new("input", "无效请求头"))?;
            result.append(name.clone(), value);
        }
    }
    Ok(result)
}
pub enum Start {
    Forward(Request, Option<Exchange>),
    Mock(WireResponse),
}
pub async fn begin(mut request: Request) -> Result<Start, ApiError> {
    let Ok(hook) = HOST_HOOK.try_with(Arc::clone) else {
        return Ok(Start::Forward(request, None));
    };
    let body = request
        .body()
        .and_then(|b| b.as_bytes())
        .unwrap_or_default();
    let description = serde_json::json!({"stage":"transport", "operation":hook.operation,
        "method":request.method().as_str(),"url":request.url().as_str(),
        "headers":headers(request.headers()),"bodyBase64":STANDARD.encode(body)});
    let mut exchange = (hook.open)(description).await?;
    let command = exchange
        .replies
        .recv()
        .await
        .ok_or_else(|| ApiError::new("cancelled", "拦截已取消"))?;
    if command["action"] == "respond" {
        return Ok(match parse_response(command["response"].clone()) {
            Ok(reply) => Start::Mock(reply),
            Err(_) => Start::Forward(request, None),
        });
    }
    let original = request.try_clone();
    let apply = (|| -> Result<(), ApiError> {
        let patch = &command["request"];
        if let Some(url) = patch["url"].as_str() {
            let url =
                reqwest::Url::parse(url).map_err(|_| ApiError::new("input", "无效拦截地址"))?;
            if !matches!(url.scheme(), "http" | "https")
                || !url.username().is_empty()
                || url.password().is_some()
            {
                return Err(ApiError::new("input", "无效拦截地址"));
            }
            *request.url_mut() = url;
        }
        if let Some(method) = patch["method"].as_str() {
            *request.method_mut() = reqwest::Method::from_bytes(method.as_bytes())
                .map_err(|_| ApiError::new("input", "无效拦截方法"))?;
        }
        if !patch["headers"].is_null() {
            *request.headers_mut() = decode_headers(&patch["headers"])?;
        }
        if let Some(body) = patch["bodyBase64"].as_str() {
            let bytes = decode_body(body)?;
            let previous = request.body().and_then(|b| b.as_bytes());
            if previous != Some(bytes.as_slice()) && !(previous.is_none() && bytes.is_empty()) {
                request
                    .headers_mut()
                    .remove(reqwest::header::CONTENT_LENGTH);
                *request.body_mut() = Some(bytes.into());
            }
        }
        Ok(())
    })();
    if apply.is_err() {
        if let Some(original) = original {
            request = original;
        } else {
            apply?;
        }
    }
    Ok(Start::Forward(request, Some(exchange)))
}
fn parse_response(value: serde_json::Value) -> Result<WireResponse, ApiError> {
    let reply: WireResponse =
        serde_json::from_value(value).map_err(|_| ApiError::new("input", "无效拦截响应"))?;
    reqwest::StatusCode::from_u16(reply.status)
        .map_err(|_| ApiError::new("input", "无效拦截状态"))?;
    decode_body(&reply.body_base64)?;
    decode_headers(&serde_json::to_value(&reply.headers).unwrap_or_default())?;
    Ok(reply)
}
pub async fn finish(
    exchange: Option<Exchange>,
    mut response: WireResponse,
) -> Result<(Vec<u8>, reqwest::StatusCode, HeaderMap), ApiError> {
    if let Some(mut exchange) = exchange {
        (exchange.notify)(1, serde_json::to_value(&response).unwrap_or_default());
        let command = exchange
            .replies
            .recv()
            .await
            .ok_or_else(|| ApiError::new("cancelled", "拦截已取消"))?;
        if let Ok(patched) = parse_response(command["response"].clone()) {
            response = patched;
        }
    }
    Ok((
        decode_body(&response.body_base64)?,
        reqwest::StatusCode::from_u16(response.status)
            .map_err(|_| ApiError::new("input", "无效状态"))?,
        decode_headers(&serde_json::to_value(response.headers).unwrap_or_default())?,
    ))
}

/// Let trusted middleware recover a transport error without ever replaying it.
/// Returning the status=0 envelope preserves the original typed diagnostics.
pub async fn fail(
    exchange: Option<Exchange>,
    error: ApiError,
) -> Result<(Vec<u8>, reqwest::StatusCode, HeaderMap), ApiError> {
    let Some(mut exchange) = exchange else {
        return Err(error);
    };
    (exchange.notify)(
        1,
        serde_json::json!({"status":0,"headers":{},"bodyBase64":"","error":&error}),
    );
    if let Some(command) = exchange.replies.recv().await {
        if let Ok(reply) = parse_response(command["response"].clone()) {
            return finish(None, reply).await;
        }
    }
    Err(error)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn hook(command: serde_json::Value) -> Arc<Hook> {
        Arc::new(Hook {
            operation: "probe".into(),
            open: Box::new(move |_| {
                let command = command.clone();
                Box::pin(async move {
                    let (tx, replies) = mpsc::unbounded_channel();
                    tx.send(command).unwrap();
                    Ok(Exchange {
                        replies,
                        notify: Box::new(|_, _| {}),
                        _guard: Box::new(()),
                    })
                })
            }),
        })
    }
    #[tokio::test]
    async fn invalid_mock_and_patch_fail_open_before_dispatch() {
        let client = reqwest::Client::new();
        let original = client.get("http://127.0.0.1/original").build().unwrap();
        let result = HOST_HOOK.scope(hook(serde_json::json!({"action":"respond", "response":{"status":200,"headers":{},"bodyBase64":"!invalid"}})), begin(original)).await.unwrap();
        assert!(matches!(result, Start::Forward(_, None)));
        let original = client.get("http://127.0.0.1/original").build().unwrap();
        let result = HOST_HOOK
            .scope(
                hook(serde_json::json!({"action":"forward", "request":{"url":"file:///escape"}})),
                begin(original),
            )
            .await
            .unwrap();
        let Start::Forward(request, Some(_)) = result else {
            panic!("expected original request");
        };
        assert_eq!(request.url().path(), "/original");
    }
    #[tokio::test]
    async fn invalid_post_forward_response_preserves_original_without_retry() {
        let reply = WireResponse {
            status: 201,
            headers: BTreeMap::new(),
            body_base64: STANDARD.encode(b"original"),
        };
        let (tx, replies) = mpsc::unbounded_channel();
        tx.send(serde_json::json!({"action":"respond","response":{"status":9999}}))
            .unwrap();
        let exchange = Exchange {
            replies,
            notify: Box::new(|stage, _| assert_eq!(stage, 1)),
            _guard: Box::new(()),
        };
        let (bytes, status, _) = finish(Some(exchange), reply).await.unwrap();
        assert_eq!(bytes, b"original");
        assert_eq!(status.as_u16(), 201);
    }
    #[test]
    fn headers_keep_repeated_values_and_reject_injection() {
        let map = decode_headers(&serde_json::json!({"set-cookie":["a=1", "b=2"]})).unwrap();
        assert_eq!(map.get_all("set-cookie").iter().count(), 2);
        assert!(decode_headers(&serde_json::json!({"x-header":["bad\r\nInjected: yes"]})).is_err());
        assert!(decode_body("not base64").is_err());
    }
}
