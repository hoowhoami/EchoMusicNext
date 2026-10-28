use serde::Serialize;
use std::time::Instant;

/// Fixed labels and counters only. Never include error strings, addresses,
/// headers or response content in the default failure record.
#[derive(Serialize)]
pub struct RequestDiagnostics {
    pub phase: &'static str,
    pub elapsed_ms: u64,
    pub http_status: Option<u16>,
    pub received_bytes: usize,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub io_kind: Option<&'static str>,
}

impl RequestDiagnostics {
    pub(crate) fn capture(
        phase: &'static str,
        started: Instant,
        http_status: Option<u16>,
        received_bytes: usize,
    ) -> Self {
        Self {
            phase,
            elapsed_ms: u64::try_from(started.elapsed().as_millis()).unwrap_or(u64::MAX),
            http_status,
            received_bytes,
            io_kind: None,
        }
    }
}

/// Errors never embed reqwest errors/URLs: signed queries can contain credentials.
#[derive(Serialize)]
pub struct ApiError {
    pub kind: &'static str,
    pub code: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub retry_after_seconds: Option<u64>,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub challenge: Option<crate::util::verification::Challenge>,
    /// Safe transport failure class for diagnostics; never carries URLs or bodies.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub transport: Option<&'static str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub diagnostics: Option<RequestDiagnostics>,
}

impl std::fmt::Debug for ApiError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ApiError")
            .field("kind", &self.kind)
            .field("code", &self.code)
            .finish_non_exhaustive()
    }
}

impl ApiError {
    pub fn new(kind: &'static str, message: impl Into<String>) -> Self {
        Self {
            kind,
            code: None,
            message: message.into(),
            retry_after_seconds: None,
            challenge: None,
            transport: None,
            diagnostics: None,
        }
    }
    pub fn business(code: i64) -> Self {
        Self {
            code: Some(code),
            ..Self::new("business", "音乐服务拒绝了请求")
        }
    }
    pub fn decode() -> Self {
        Self::new("decode", "无法解码接口返回的数据")
    }
    pub fn resource_limit() -> Self {
        Self::new("resource_limit", "接口资源超过解码内存预算")
    }
    pub fn encoding() -> Self {
        Self::new("internal", "无法编码接口请求数据")
    }
    pub fn numeric_overflow() -> Self {
        Self::new("numeric_overflow", "接口数值超出目标单位可表示范围")
    }
    pub fn authentication(code: i64) -> Self {
        Self {
            code: Some(code),
            ..Self::new("authentication", "账号认证无效，请重新登录")
        }
    }
    pub fn challenge(challenge: crate::util::verification::Challenge) -> Self {
        Self {
            challenge: Some(challenge),
            ..Self::new("challenge", "需要完成账号安全验证")
        }
    }
    pub(crate) fn with_diagnostics(mut self, diagnostics: RequestDiagnostics) -> Self {
        self.diagnostics = Some(diagnostics);
        self
    }
}
