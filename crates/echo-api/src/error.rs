use serde::Serialize;

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
        }
    }
    pub fn business(code: i64) -> Self {
        Self {
            kind: "business",
            code: Some(code),
            message: "音乐服务拒绝了请求".into(),
            retry_after_seconds: None,
            challenge: None,
            transport: None,
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
            kind: "authentication",
            code: Some(code),
            message: "账号认证无效，请重新登录".into(),
            retry_after_seconds: None,
            challenge: None,
            transport: None,
        }
    }
    pub fn challenge(challenge: crate::util::verification::Challenge) -> Self {
        Self {
            kind: "challenge",
            code: None,
            message: "需要完成账号安全验证".into(),
            retry_after_seconds: None,
            challenge: Some(challenge),
            transport: None,
        }
    }
}
