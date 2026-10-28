use crate::{
    error::{ApiError, RequestDiagnostics},
    util::signature,
};
use reqwest::{Client, Method};
use serde::{Deserialize, de::DeserializeOwned};
use std::{
    collections::BTreeMap,
    sync::{
        RwLock,
        atomic::{AtomicU64, Ordering},
    },
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

/// Caller-owned account/device snapshot. Intentionally no Debug/Serialize derive.
#[derive(Clone, Deserialize)]
#[serde(from = "IdentityInput")]
pub struct Identity {
    pub userid: u64,
    pub token: String,
    pub t1: String,
    pub mid: String,
    pub dfid: String,
    pub uuid: String,
    pub guid: String,
    pub mac: String,
    pub device_name: String,
    pub render_fingerprint: String,
}

/// Mirrors credential-injection semantics, not upstream signature suppression.
/// IndependentDevice must receive a separate host-owned anonymous device snapshot.
#[derive(Clone, Copy, Default, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AuthenticationMode {
    #[default]
    CurrentAccount,
    DeviceOnly,
    IndependentDevice,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct IdentityInput {
    #[serde(default)]
    pub userid: u64,
    #[serde(default)]
    pub token: String,
    #[serde(default)]
    t1: String,
    pub mid: String,
    pub dfid: String,
    #[serde(default = "absent_uuid")]
    uuid: String,
    #[serde(default)]
    guid: String,
    #[serde(default)]
    mac: String,
    #[serde(default)]
    device_name: String,
    #[serde(default)]
    render_fingerprint: String,
    #[serde(default)]
    authentication: AuthenticationMode,
}
fn absent_uuid() -> String {
    "-".into()
}
impl From<IdentityInput> for Identity {
    fn from(input: IdentityInput) -> Self {
        let include_account = matches!(input.authentication, AuthenticationMode::CurrentAccount);
        Self {
            userid: if include_account { input.userid } else { 0 },
            token: if include_account {
                input.token
            } else {
                String::new()
            },
            t1: if include_account {
                input.t1
            } else {
                String::new()
            },
            mid: input.mid,
            dfid: input.dfid,
            uuid: input.uuid,
            guid: input.guid,
            mac: input.mac,
            device_name: input.device_name,
            render_fingerprint: input.render_fingerprint,
        }
    }
}

pub struct ApiClient {
    http: RwLock<Client>,
    timeout_ms: AtomicU64,
    base_url: String,
    pub(crate) listen_sessions:
        tokio::sync::Mutex<BTreeMap<String, crate::module::user_listen_report::Session>>,
}
/// Endpoint-controlled transport settings, never arbitrary URLs from QML.
pub struct Endpoint<'a> {
    pub origin: Origin,
    pub path: &'a str,
    pub router: Option<&'a str>,
    pub method: Method,
    pub params: BTreeMap<String, String>,
    pub body: Vec<u8>,
    pub headers: &'a [(&'a str, &'a str)],
}
pub struct HttpResponse {
    pub bytes: Vec<u8>,
    pub http_status: u16,
    pub request_parameters: BTreeMap<String, String>,
}

pub enum Origin {
    DeviceLogout,
    RelationUser,
    Climax,
    Lyrics,
    EffectsCatalog,
    Gateway,
    Tracker,
    Verification,
    UserService,
    Registration,
    QrLogin,
    Vip,
    LoginRetry,
    /// mcloudservice.kugou.com direct host: playlist AES body plus its own
    /// parameter set; no Android signature, no injected userid/token query.
    McloudService,
    SumComment,
    VideoCollect,
}
pub(crate) fn transport_error(
    error: reqwest::Error,
    fallback: &'static str,
    mut diagnostics: RequestDiagnostics,
) -> ApiError {
    // Inspect typed causes, never format them: reqwest and TLS error strings
    // can embed a signed URL or provider-controlled text.
    let mut cause = std::error::Error::source(&error);
    for _ in 0..16 {
        let Some(source) = cause else { break };
        if let Some(io) = source.downcast_ref::<std::io::Error>() {
            use std::io::ErrorKind;
            diagnostics.io_kind = Some(match io.kind() {
                ErrorKind::ConnectionRefused => "connection_refused",
                ErrorKind::ConnectionReset => "connection_reset",
                ErrorKind::ConnectionAborted => "connection_aborted",
                ErrorKind::NotConnected => "not_connected",
                ErrorKind::BrokenPipe => "broken_pipe",
                ErrorKind::TimedOut => "timed_out",
                ErrorKind::UnexpectedEof => "unexpected_eof",
                ErrorKind::PermissionDenied => "permission_denied",
                ErrorKind::InvalidData => "invalid_data",
                _ => "other",
            });
            break;
        }
        cause = source.source();
    }
    if error.is_timeout() {
        let mut failure = ApiError::new("transport", "网络请求超时");
        failure.transport = Some("timeout");
        return failure.with_diagnostics(diagnostics);
    }
    let class = if error.is_connect() {
        "connect"
    } else if error.is_body() {
        "body"
    } else if error.is_decode() {
        "decode"
    } else if error.is_request() {
        "request"
    } else if error.is_redirect() {
        "redirect"
    } else if error.is_builder() {
        "builder"
    } else {
        "other"
    };
    let mut failure = ApiError::new("transport", fallback);
    failure.transport = Some(class);
    failure.with_diagnostics(diagnostics)
}
impl ApiClient {
    fn http_builder() -> reqwest::ClientBuilder {
        Client::builder()
            // Reuse connections for short request bursts without retaining a
            // large idle pool. This is a local resource policy, not a claim
            // about the upstream's idle timeout. Active requests are unaffected.
            .pool_idle_timeout(Duration::from_secs(10))
            .pool_max_idle_per_host(4)
            // Retain the client's safe protocol recovery defaults; do not add
            // a general transport/status retry that could replay a write.
            // The Qt host is the sole route owner, including direct mode.
            .no_proxy()
            // Never forward signed credentials to another host on redirect.
            .redirect(reqwest::redirect::Policy::none())
    }
    /// Comment count uses the reference web signature, retaining the lite defaults.
    pub async fn request_comment_count(
        &self,
        identity: &Identity,
        params: BTreeMap<String, String>,
    ) -> Result<Vec<u8>, ApiError> {
        self.request_endpoint(
            identity,
            Endpoint {
                origin: Origin::SumComment,
                path: "/index.php",
                router: Some("sum.comment.service.kugou.com"),
                method: Method::GET,
                params,
                body: Vec::new(),
                headers: &[],
            },
        )
        .await
    }
    /// Video metadata authenticates in its signed JSON body; no URL defaults.
    pub async fn request_video_detail(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, false, true)
            .await?
            .bytes)
    }
    /// Comment writes/list-barrage have their own timestamp proof and no Android signature.
    pub async fn request_comment(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, true, true)
            .await?
            .bytes)
    }
    /// Relation writes carry authentication in body key/p, without defaults/signature.
    pub async fn request_relation(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, true, true)
            .await?
            .bytes)
    }
    pub fn new() -> Result<Self, ApiError> {
        let http = Self::http_builder()
            .build()
            .map_err(|_| ApiError::new("transport", "Cannot initialize HTTP transport"))?;
        Ok(Self {
            http: RwLock::new(http),
            timeout_ms: AtomicU64::new(0),
            base_url: "https://gateway.kugou.com".into(),
            listen_sessions: tokio::sync::Mutex::new(BTreeMap::new()),
        })
    }

    pub(crate) fn http(&self) -> Client {
        self.http.read().unwrap_or_else(|e| e.into_inner()).clone()
    }

    /// A host-owned loopback transport supplies all route decisions. Reject
    /// arbitrary upstream URLs here: proxy credentials never enter API URLs.
    pub fn set_transport_port(&self, port: u16) -> bool {
        if port == 0 {
            return false;
        }
        let Ok(proxy) = reqwest::Proxy::all(format!("http://127.0.0.1:{port}")) else {
            return false;
        };
        let Ok(client) = Self::http_builder().proxy(proxy).build() else {
            return false;
        };
        *self.http.write().unwrap_or_else(|e| e.into_inner()) = client;
        true
    }

    /// Host-owned settings affect builders for new requests only. No request
    /// in flight reads this value again, including while streaming its body.
    pub fn set_timeout_seconds(&self, seconds: f64) -> bool {
        if !seconds.is_finite() || !(0.0..=300.0).contains(&seconds) {
            return false;
        }
        self.timeout_ms
            .store((seconds * 1000.0).round() as u64, Ordering::Release);
        true
    }

    pub(crate) fn apply_timeout(
        &self,
        request: reqwest::RequestBuilder,
    ) -> reqwest::RequestBuilder {
        let millis = self.timeout_ms.load(Ordering::Acquire);
        if millis == 0 {
            request
        } else {
            request.timeout(Duration::from_millis(millis))
        }
    }

    pub async fn request(
        &self,
        identity: &Identity,
        path: &str,
        router: Option<&str>,
        method: Method,
        params: BTreeMap<String, String>,
        body: Vec<u8>,
    ) -> Result<Vec<u8>, ApiError> {
        self.request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path,
                router,
                method,
                params,
                body,
                headers: &[],
            },
        )
        .await
    }

    pub async fn request_endpoint(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .request_endpoint_with_metadata(identity, endpoint)
            .await?
            .bytes)
    }
    /// Preserve observed status for development raw-protocol capture. No Debug:
    /// the response bytes may hold account or device credentials.
    pub async fn request_endpoint_with_metadata(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<HttpResponse, ApiError> {
        self.send_endpoint(identity, endpoint, false, false).await
    }
    /// Lyric search clears device/account URL defaults but retains the Android
    /// parameter signature. Download uses the ordinary signed request path.
    pub async fn request_lyric_search(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, false, true)
            .await?
            .bytes)
    }
    /// Public effect catalogs use a signed query without default identity fields.
    pub async fn request_effect_catalog(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, false, true)
            .await?
            .bytes)
    }
    /// Import authenticates in the JSON body. Its URL contains only the six
    /// device/application fields and signature, without account defaults.
    pub async fn request_import(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, false, true)
            .await?
            .bytes)
    }
    /// Cloudlist binary protocol authenticates inside RSA portrait, signs with
    /// its timestamp key, and has neither Android signature nor plaintext token.
    pub async fn request_cloud_with_metadata(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<HttpResponse, ApiError> {
        self.send_endpoint(identity, endpoint, true, false).await
    }
    /// Grade query has its own body timestamp key and only dfid in the URL.
    /// This is endpoint-owned, never a caller-controlled signature bypass.
    pub async fn request_grade(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, true, true)
            .await?
            .bytes)
    }
    /// mcloudservice host transport: the playlist AES/RSA protocol with an
    /// endpoint-owned parameter set. clear_defaults excludes every injected
    /// account/device query value; identity travels inside the RSA portrait.
    pub async fn request_mcloud(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
    ) -> Result<Vec<u8>, ApiError> {
        Ok(self
            .send_endpoint(identity, endpoint, true, true)
            .await?
            .bytes)
    }
    /// Endpoint-owned BSS upload transport. Caller validates the returned BSS host.
    pub(crate) async fn request_cloud_upload(
        &self,
        url: &str,
        method: Method,
        params: BTreeMap<String, String>,
        authorization: &str,
        body: Vec<u8>,
    ) -> Result<Vec<u8>, ApiError> {
        use rsa::rand_core::{OsRng, RngCore};
        let trace_hash = format!("{:07x}", OsRng.next_u32() % 0x0fff_ffff);
        let empty_post = method == Method::POST && body.is_empty();
        let mut request = self
            .http()
            .request(method, url)
            .query(&params)
            .header("User-Agent", "Android15-1070-11440-201-0-wifi")
            .header("KG-RC", "1")
            .header("KG-Rec", "1")
            .header("KG-THash", trace_hash);
        if !authorization.is_empty() {
            request = request.header("Authorization", authorization);
        }
        if empty_post {
            // Axios sends Content-Length: 0 for initiate/complete. BSS complete
            // rejects an unframed empty HTTP/2 POST with 411.
            request = request.header("Content-Length", "0").body(Vec::new());
        } else if !body.is_empty() {
            request = request
                .header("Content-Type", "application/octet-stream")
                .body(body);
        }
        Ok(self
            .receive(self.apply_timeout(request), BTreeMap::new())
            .await?
            .bytes)
    }
    async fn send_endpoint(
        &self,
        identity: &Identity,
        endpoint: Endpoint<'_>,
        cloud: bool,
        clear_defaults: bool,
    ) -> Result<HttpResponse, ApiError> {
        let Endpoint {
            origin,
            path,
            router,
            method,
            mut params,
            body,
            headers,
        } = endpoint;
        if identity.mid.is_empty()
            || identity.dfid.is_empty()
            || (identity.userid == 0) != identity.token.is_empty()
            || (identity.userid == 0 && !identity.t1.is_empty())
        {
            return Err(ApiError::new("input", "账号或设备上下文不完整"));
        }
        let timestamp = unix_seconds()?.to_string();
        // Exact lite protocol defaults; endpoint values override them (e.g. search clientver=201).
        let mut defaults = BTreeMap::from([
            ("appid".into(), "3116".into()),
            ("clientver".into(), "11440".into()),
            ("clienttime".into(), timestamp.clone()),
            ("dfid".into(), identity.dfid.clone()),
            ("mid".into(), identity.mid.clone()),
            // The lite Android transport fixes uuid='-'. Retain the registered
            // device UUID in the snapshot for endpoints that explicitly use it.
            ("uuid".into(), "-".into()),
        ]);
        if cloud && !matches!(origin, Origin::VideoCollect) {
            defaults.remove("uuid");
        }
        if clear_defaults {
            defaults.clear();
        }
        if (!cloud || matches!(origin, Origin::VideoCollect))
            && !clear_defaults
            && !identity.token.is_empty()
        {
            defaults.insert("token".into(), identity.token.clone());
        }
        if (!cloud || matches!(origin, Origin::VideoCollect))
            && !clear_defaults
            && identity.userid != 0
        {
            defaults.insert("userid".into(), identity.userid.to_string());
        }
        defaults.append(&mut params);
        if !cloud {
            let signed = if matches!(
                origin,
                Origin::QrLogin | Origin::DeviceLogout | Origin::SumComment
            ) {
                signature::web(&defaults, &body)
            } else {
                signature::android(&defaults, &body)
            };
            defaults.insert("signature".into(), signed);
        }
        let query =
            if matches!(origin, Origin::Registration) {
                // EchoMusic sends defaults then endpoint parameters, signature last.
                const ORDER: [&str; 9] = [
                    "dfid",
                    "mid",
                    "uuid",
                    "appid",
                    "clientver",
                    "clienttime",
                    "part",
                    "platid",
                    "p",
                ];
                let mut query = ORDER
                    .into_iter()
                    .filter_map(|key| defaults.get_key_value(key))
                    .collect::<Vec<_>>();
                query.extend(defaults.iter().filter(|(key, _)| {
                    key.as_str() != "signature" && !ORDER.contains(&key.as_str())
                }));
                query.extend(defaults.get_key_value("signature"));
                query
            } else {
                defaults.iter().collect::<Vec<_>>()
            };
        let mut request = self.apply_timeout(
            self.http()
                .request(
                    method,
                    format!(
                        "{}{path}",
                        match origin {
                            Origin::Lyrics => "https://lyrics.kugou.com",
                            Origin::RelationUser => "https://relationuser.kugou.com",
                            Origin::Climax => "https://expendablekmrcdn.kugou.com",
                            // The reference public catalog uses HTTP; this CDN has no matching
                            // TLS certificate. clear_defaults excludes token/userid entirely.
                            Origin::EffectsCatalog => "http://mobilecdngz.kugou.com",
                            Origin::McloudService => "https://mcloudservice.kugou.com",
                            Origin::VideoCollect => "https://collectservice.kugou.com",
                            Origin::Gateway | Origin::DeviceLogout | Origin::SumComment =>
                                self.base_url.as_str(),
                            Origin::Tracker => "https://trackercdngz.kugou.com",
                            Origin::Verification => "https://verifyservice.kugou.com",
                            Origin::UserService | Origin::Registration =>
                                "https://userservice.kugou.com",
                            Origin::QrLogin => "https://login-user.kugou.com",
                            Origin::Vip => "https://kugouvip.kugou.com",
                            Origin::LoginRetry => "https://loginserviceretry.kugou.com",
                        }
                    ),
                )
                .query(&query)
                .header(
                    "User-Agent",
                    headers
                        .iter()
                        .find(|(name, _)| name.eq_ignore_ascii_case("user-agent"))
                        .map_or(
                            "Android15-1070-11083-46-0-DiscoveryDRADProtocol-wifi",
                            |(_, value)| *value,
                        ),
                )
                .header("dfid", &identity.dfid)
                .header("mid", &identity.mid),
        );
        // Endpoints without a query timestamp, including video metadata and MV
        // barrage writes, omit the clienttime header.
        if let Some(value) = defaults.get("clienttime") {
            request = request.header("clienttime", value);
        }
        if let Some(router) = router {
            request = request.header("x-router", router);
        }
        for &(name, value) in headers {
            if !name.eq_ignore_ascii_case("content-type")
                && !name.eq_ignore_ascii_case("user-agent")
            {
                request = request.header(name, value);
            }
        }
        if !body.is_empty() {
            request = request
                .header(
                    "Content-Type",
                    headers
                        .iter()
                        .find(|(name, _)| name.eq_ignore_ascii_case("content-type"))
                        .map_or("application/json", |(_, value)| *value),
                )
                .body(body);
        }
        self.receive(request, defaults).await
    }
    /// CSCC has its own signed binary body and query. Never inject Android
    /// defaults, account cookies, or gateway routing into this direct host.
    pub(crate) async fn request_cscc(
        &self,
        path: &str,
        method: Method,
        params: BTreeMap<String, String>,
        body: Vec<u8>,
    ) -> Result<Vec<u8>, ApiError> {
        if !matches!(path, "/v3/qrydid" | "/v2/gen" | "/v2/post") {
            return Err(ApiError::new("input", "无效上报路径"));
        }
        let post = method == Method::POST;
        let mut request = self
            .http()
            .request(method, format!("http://d.kugou.com{path}"))
            .query(&params);
        if path != "/v3/qrydid" {
            request = request.header("KG-Rec", "1").header(
                "User-Agent",
                if path == "/v2/post" {
                    "Android9-1070-10597-18-0-CsccPost-wifi"
                } else {
                    "Android9-1070-10597-18-0-CsccGen-wifi"
                },
            );
        }
        if post {
            request = request
                .header("Content-Type", "application/octet-stream")
                .body(body);
        }
        Ok(self
            .receive(self.apply_timeout(request), BTreeMap::new())
            .await?
            .bytes)
    }
    /// Fixed image origin. This separate request has no identity, device defaults,
    /// Android signature, cookies or authentication headers, even when logged in.
    pub(crate) async fn upload_playlist_png(
        &self,
        png: Vec<u8>,
        params: BTreeMap<String, String>,
    ) -> Result<HttpResponse, ApiError> {
        let request = self.image_request(png, &params);
        self.receive(request, params).await
    }
    /// Avatar multipart upload uses a fixed image host without user or device authentication.
    pub(crate) async fn upload_avatar(
        &self,
        body: Vec<u8>,
        content_type: String,
    ) -> Result<Vec<u8>, ApiError> {
        let request = self.apply_timeout(
            self.http()
                .post("http://imgphp.kugou.com/imageupload/post.php")
                .header("Content-Type", content_type)
                .body(body),
        );
        Ok(self.receive(request, BTreeMap::new()).await?.bytes)
    }
    /// Fixed public artwork origin and standard signature. Optional artwork
    /// must never inject account credentials or invoke a login challenge flow.
    pub async fn album_dynamic_cover(&self, audio: u64, album: u64) -> Result<Vec<u8>, ApiError> {
        let mut item = serde_json::json!({"album_audio_id": audio});
        if album > 0 {
            item["album_id"] = serde_json::json!(album);
        }
        let mut params = BTreeMap::from([
            ("appid".into(), "1005".into()),
            ("clientver".into(), "20489".into()),
            (
                "data".into(),
                serde_json::to_string(&vec![item]).map_err(|_| ApiError::encoding())?,
            ),
            ("isCdn".into(), "1".into()),
            ("query".into(), "audioPlay".into()),
        ]);
        params.insert("signature".into(), signature::album_cover(&params));
        let request = self.apply_timeout(
            self.http()
                .get("https://kmrcdn.service.kugou.com/v2/album/audio")
                .query(&params)
                .header("dfid", "-")
                .header("mid", "undefined")
                .header("clienttime", (unix_milliseconds()? / 1000).to_string()),
        );
        Ok(self.receive(request, params).await?.bytes)
    }

    fn image_request(
        &self,
        png: Vec<u8>,
        params: &BTreeMap<String, String>,
    ) -> reqwest::RequestBuilder {
        // Playlist image upload has an endpoint-specific 30-second timeout.
        self.http()
            .post("http://imgphp.kugou.com/imageupload/stream.php")
            .timeout(Duration::from_secs(30))
            .query(params)
            .header("Content-Type", "application/octet-stream")
            .body(png)
    }
    async fn receive(
        &self,
        request: reqwest::RequestBuilder,
        parameters: BTreeMap<String, String>,
    ) -> Result<HttpResponse, ApiError> {
        let started = Instant::now();
        use base64::Engine;
        let (client, built) = request.build_split();
        let request = built.map_err(|_| ApiError::new("input", "无效接口请求"))?;
        let diagnostic_path = request.url().path().to_owned();
        let intercepted = super::interception::HOST_HOOK
            .try_with(|_| true)
            .unwrap_or(false);
        let (bytes, status, headers) = match super::interception::begin(request).await? {
            super::interception::Start::Mock(reply) => {
                super::interception::finish(None, reply).await?
            }
            super::interception::Start::Forward(request, exchange) => {
                let received = async {
                    let mut response = client.execute(request).await.map_err(|error| {
                        transport_error(
                            error,
                            "网络请求失败",
                            RequestDiagnostics::capture("send", started, None, 0),
                        )
                    })?;
                    let status = response.status();
                    let response_headers = response.headers().clone();
                    let diagnostics = |phase, bytes| {
                        RequestDiagnostics::capture(phase, started, Some(status.as_u16()), bytes)
                    };

                    // Refuse arbitrarily large bodies. Decode errors do not expose raw account data.
                    if response
                        .content_length()
                        .is_some_and(|len| len > 16 * 1024 * 1024)
                    {
                        return Err(ApiError::new("limit", "接口报文过大")
                            .with_diagnostics(diagnostics("response_body", 0)));
                    }
                    let mut bytes = Vec::new();
                    while let Some(chunk) = response.chunk().await.map_err(|error| {
                        transport_error(
                            error,
                            "读取接口报文失败",
                            diagnostics("response_body", bytes.len()),
                        )
                    })? {
                        if chunk.len() > 16 * 1024 * 1024 - bytes.len() {
                            return Err(ApiError::new("limit", "接口报文过大")
                                .with_diagnostics(diagnostics("response_body", bytes.len())));
                        }
                        bytes.extend_from_slice(&chunk);
                    }
                    Ok::<_, ApiError>((bytes, status, response_headers))
                }
                .await;
                match received {
                    Err(error) => super::interception::fail(exchange, error).await?,
                    Ok((bytes, status, response_headers)) => {
                        if exchange.is_some() {
                            super::interception::finish(
                                exchange,
                                super::interception::WireResponse {
                                    status: status.as_u16(),
                                    headers: super::interception::headers(&response_headers),
                                    body_base64: base64::engine::general_purpose::STANDARD
                                        .encode(&bytes),
                                },
                            )
                            .await?
                        } else {
                            (bytes, status, response_headers)
                        }
                    }
                }
            }
        };
        let diagnostics = |phase, count| {
            RequestDiagnostics::capture(phase, started, Some(status.as_u16()), count)
        };
        let retry_after_seconds = headers
            .get("retry-after")
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.trim().parse::<u64>().ok());
        let event = headers
            .get("ssa-code")
            .and_then(|v| v.to_str().ok())
            .map(str::to_owned);
        // Handshake bodies contain session credentials even under generic data
        // keys. They must never enter response diagnostics.
        if !matches!(
            diagnostic_path.as_str(),
            "/v2/gen"
                | "/v3/qrydid"
                | "/bsstrackercdngz/v1/upload/auth"
                | "/v2/multipart/initiate/music"
                | "/v3/multipart/upload"
                | "/v3/multipart/complete"
        ) {
            if intercepted {
                super::response_logging::record_intercepted(
                    &bytes,
                    status.as_u16(),
                    &diagnostic_path,
                );
            } else {
                super::response_logging::record(&bytes, status.as_u16(), &diagnostic_path);
            }
        }
        // Inspect challenge metadata before HTTP/business converters. Headers
        // are case-insensitive; never discard the event or fabricate sid/edt.
        if let Some(challenge) = super::verification::challenge(event.as_deref(), &bytes) {
            let mut error = ApiError::challenge(challenge);
            // The header is an event identifier, not an implied business code.
            // Preserve an actual numeric error_code only when present in the body.
            error.code = serde_json::from_slice::<serde_json::Value>(&bytes)
                .ok()
                .and_then(|v| v["error_code"].as_i64());
            return Err(error.with_diagnostics(diagnostics("response_status", bytes.len())));
        }
        if !status.is_success() {
            let mut error = ApiError::new("http", "音乐服务 HTTP 请求失败");
            error.code = Some(i64::from(status.as_u16()));
            error.retry_after_seconds = retry_after_seconds;
            return Err(error.with_diagnostics(diagnostics("response_status", bytes.len())));
        }
        Ok(HttpResponse {
            bytes,
            http_status: status.as_u16(),
            request_parameters: parameters,
        })
    }
}

pub fn unix_milliseconds() -> Result<u64, ApiError> {
    let time = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| ApiError::new("clock", "系统时钟无效"))?
        .as_millis();
    u64::try_from(time).map_err(|_| ApiError::new("clock", "系统时钟无效"))
}

pub fn unix_seconds() -> Result<u64, ApiError> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|time| time.as_secs())
        .map_err(|_| ApiError::new("clock", "系统时钟无效"))
}
pub fn decode<T: DeserializeOwned>(bytes: &[u8]) -> Result<T, ApiError> {
    serde_json::from_slice(bytes).map_err(|_| ApiError::decode())
}
/// Report only whitelisted protocol field names and array positions. Never
/// include a rejected value or arbitrary map key in logs/user-facing errors.
pub fn decode_value<T: DeserializeOwned>(
    value: serde_json::Value,
    fields: &[&str],
) -> Result<T, ApiError> {
    serde_path_to_error::deserialize(value).map_err(|error| {
        let path = error
            .path()
            .iter()
            .take(16)
            .map(|segment| match segment {
                serde_path_to_error::Segment::Seq { index } => format!("[{index}]"),
                serde_path_to_error::Segment::Map { key } if fields.contains(&key.as_str()) => {
                    key.clone()
                }
                serde_path_to_error::Segment::Enum { variant }
                    if fields.contains(&variant.as_str()) =>
                {
                    variant.clone()
                }
                _ => "*".into(),
            })
            .collect::<Vec<_>>()
            .join(".");
        ApiError::new(
            "decode",
            format!(
                "无法解码接口响应字段 {}",
                if path.is_empty() { "*" } else { &path }
            ),
        )
    })
}
pub fn encode<T: serde::Serialize>(value: &T) -> Result<Vec<u8>, ApiError> {
    serde_json::to_vec(value)
        .map_err(|_| ApiError::new("internal", "Cannot encode domain response"))
}
pub fn success(status: u32, code: i64) -> Result<(), ApiError> {
    if status == 1 && code == 0 {
        Ok(())
    } else {
        Err(ApiError::business(code))
    }
}

#[cfg(test)]
mod tests {
    #[tokio::test]
    async fn bss_complete_sends_explicit_zero_length_post_and_preserves_retry_after() {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            stream
                .set_read_timeout(Some(Duration::from_secs(3)))
                .unwrap();
            let mut input = Vec::new();
            while !input.windows(4).any(|v| v == b"\r\n\r\n") {
                let mut b = [0; 2048];
                let n = stream.read(&mut b).unwrap();
                assert!(n > 0);
                input.extend_from_slice(&b[..n]);
            }
            stream.write_all(b"HTTP/1.1 429 Too Many Requests\r\nContent-Length: 2\r\nRetry-After: 90\r\nConnection: close\r\n\r\n{}").unwrap();
            String::from_utf8(input).unwrap()
        });
        let client = ApiClient::new().unwrap();
        assert!(client.set_transport_port(address.port()));
        let error = client
            .request_cloud_upload(
                "http://bssulbig.kugou.com/v3/multipart/complete",
                Method::POST,
                BTreeMap::new(),
                "fixture-proof",
                Vec::new(),
            )
            .await
            .unwrap_err();
        assert_eq!(error.code, Some(429));
        assert_eq!(error.retry_after_seconds, Some(90));
        let sent = server.join().unwrap().to_ascii_lowercase();
        assert!(sent.starts_with("post "));
        assert!(sent.contains("\r\ncontent-length: 0\r\n"));
        assert!(sent.contains("\r\nauthorization: fixture-proof\r\n"));
    }
    #[tokio::test]
    async fn configured_timeout_covers_body_and_keeps_existing_request_snapshot() {
        use std::net::TcpListener;
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            for _ in 0..2 {
                let (mut stream, _) = listener.accept().unwrap();
                stream
                    .set_read_timeout(Some(Duration::from_secs(2)))
                    .unwrap();
                let mut input = [0_u8; 2048];
                let _ = stream.read(&mut input).unwrap();
                stream
                    .write_all(
                        b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nx",
                    )
                    .unwrap();
                // Transport-only body, never a fixture for an upstream parser.
                std::thread::sleep(Duration::from_millis(150));
                let _ = stream.write_all(b"x");
            }
        });
        let client = ApiClient::new().unwrap();
        for seconds in [f64::NAN, f64::INFINITY, -1.0, 301.0] {
            assert!(!client.set_timeout_seconds(seconds));
        }
        assert!(client.set_timeout_seconds(0.04));
        let pending = client.apply_timeout(client.http().get(format!("http://{address}/limited")));
        assert!(client.set_timeout_seconds(0.0));
        let error = match client.receive(pending, BTreeMap::new()).await {
            Ok(_) => panic!("captured deadline was removed from an existing builder"),
            Err(error) => error,
        };
        assert_eq!(error.kind, "transport");
        assert_eq!(error.message, "网络请求超时");
        assert_eq!(error.transport, Some("timeout"));
        let unbounded =
            client.apply_timeout(client.http().get(format!("http://{address}/unbounded")));
        let response = client.receive(unbounded, BTreeMap::new()).await.unwrap();
        assert_eq!(response.bytes, b"xx");
        server.join().unwrap();
        let upload = client
            .image_request(vec![1], &BTreeMap::new())
            .build()
            .unwrap();
        assert_eq!(upload.timeout(), Some(&Duration::from_secs(30)));
        assert!(client.set_timeout_seconds(1.25));
        let upload = client
            .image_request(vec![1], &BTreeMap::new())
            .build()
            .unwrap();
        assert_eq!(upload.timeout(), Some(&Duration::from_secs(30)));
    }
    #[tokio::test]
    async fn closed_endpoint_reports_a_safe_connect_class() {
        use std::net::TcpListener;
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        drop(listener);
        let client = ApiClient::new().unwrap();
        let error = match client
            .receive(
                client.http().get(format!("http://{address}/probe")),
                BTreeMap::new(),
            )
            .await
        {
            Ok(_) => panic!("closed endpoint unexpectedly returned a response"),
            Err(error) => error,
        };
        assert_eq!(error.kind, "transport");
        assert_eq!(error.transport, Some("connect"));
        assert!(!error.message.contains(&address.to_string()));
    }
    #[test]
    fn image_upload_has_only_its_fixed_origin_query_and_raw_body() {
        let client = super::ApiClient::new().unwrap();
        let params = std::collections::BTreeMap::from([
            ("type".into(), "custom".into()),
            ("extendName".into(), ".png".into()),
            ("md5".into(), "fixture-checksum".into()),
            ("jsonResponse".into(), "1".into()),
        ]);
        let request = client
            .image_request(vec![0, 255, 128], &params)
            .build()
            .unwrap();
        assert_eq!(request.url().host_str(), Some("imgphp.kugou.com"));
        assert_eq!(request.url().path(), "/imageupload/stream.php");
        assert_eq!(request.url().query_pairs().count(), 4);
        for header in [
            "authorization",
            "cookie",
            "mid",
            "dfid",
            "clienttime",
            "x-router",
        ] {
            assert!(!request.headers().contains_key(header));
        }
        assert_eq!(
            request.headers()["content-type"],
            "application/octet-stream"
        );
        assert_eq!(request.body().unwrap().as_bytes().unwrap(), [0, 255, 128]);
    }
    use super::*;
    use std::io::{Read, Write};

    #[tokio::test]
    async fn registration_binary_body_is_preserved_independently_of_response_mime() {
        use super::super::crypto::PlaylistEnvelope;
        for mime in ["application/octet-stream", "application/json", "text/json"] {
            let plain =
                serde_json::json!({"status":1,"error_code":0,"data":{"dfid":"test-identity"}});
            let envelope =
                PlaylistEnvelope::registration_with_seed(&plain, "abc123".into()).unwrap();
            let body = envelope.body.clone();
            let expected = body.clone();
            let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
            let address = listener.local_addr().unwrap();
            let server = std::thread::spawn(move || {
                let (mut socket, _) = listener.accept().unwrap();
                socket
                    .set_read_timeout(Some(Duration::from_secs(3)))
                    .unwrap();
                let mut request = [0; 4096];
                assert!(socket.read(&mut request).unwrap() > 0);
                write!(socket,"HTTP/1.1 200 OK\r\nContent-Type: {mime}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",body.len()).unwrap();
                // Binary chunks, including arbitrary non-UTF8 bytes, must not
                // pass through a JSON/text conversion in the transport.
                for chunk in body.chunks(7) {
                    socket.write_all(chunk).unwrap();
                }
            });
            let client = ApiClient::new().unwrap();
            let response = client
                .receive(
                    client
                        .http()
                        .get(format!("http://{address}/risk/v3/r_register_dev")),
                    BTreeMap::new(),
                )
                .await
                .unwrap();
            assert_eq!(response.bytes, expected);
            let decrypted = envelope.decrypt(&response.bytes).unwrap();
            assert_eq!(
                crate::module::register_dev::convert(&decrypted)
                    .unwrap()
                    .dfid,
                "test-identity"
            );
            server.join().unwrap();
        }
    }

    #[tokio::test]
    #[cfg(feature = "local-api-fixtures")]
    async fn grade_query_keeps_endpoint_body_key_and_clears_default_url_signature() {
        let fixture: serde_json::Value = serde_json::from_slice(include_bytes!(
            "../../../../tests/fixtures/api/user_grade_info/explicit-user-device-only.json"
        ))
        .unwrap();
        let body = serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap();
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut incoming = Vec::new();
            loop {
                let mut buffer = [0; 2048];
                let count = socket.read(&mut buffer).unwrap();
                assert!(count > 0);
                incoming.extend_from_slice(&buffer[..count]);
                if let Some(end) = incoming.windows(4).position(|v| v == b"\r\n\r\n") {
                    let headers = String::from_utf8_lossy(&incoming[..end]).to_ascii_lowercase();
                    let length: usize = headers
                        .lines()
                        .find_map(|l| l.strip_prefix("content-length: "))
                        .unwrap()
                        .parse()
                        .unwrap();
                    if incoming.len() >= end + 4 + length {
                        break;
                    }
                }
            }
            write!(
                socket,
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                body.len()
            )
            .unwrap();
            socket.write_all(&body).unwrap();
            String::from_utf8(incoming).unwrap()
        });
        let identity:Identity=serde_json::from_value(serde_json::json!({"userid":123,"token":"fixture-secret","t1":"fixture-refresh","mid":"fixture-mid","dfid":"fixture-dfid","uuid":"fixture-uuid","authentication":"device_only"})).unwrap();
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        crate::module::user_grade_info::execute(
            &client,
            &identity,
            crate::module::user_grade_info::Query {
                user_id: Some(900000),
            },
        )
        .await
        .unwrap();
        let sent = server.join().unwrap();
        assert!(sent.starts_with("POST /v2/get_grade_info?dfid=fixture-dfid HTTP/1.1"));
        let (headers, body) = sent.split_once("\r\n\r\n").unwrap();
        let headers = headers.to_ascii_lowercase();
        assert!(headers.contains("x-router: userinfo.user.kugou.com"));
        let nonce = headers
            .lines()
            .find_map(|l| l.strip_prefix("kg-thash: "))
            .unwrap();
        assert_eq!(nonce.len(), 7);
        assert!(nonce.bytes().all(|b| b.is_ascii_hexdigit()));
        for excluded in ["signature=", "token=", "fixture-secret", "fixture-refresh"] {
            assert!(!sent.contains(excluded));
        }
        let data: serde_json::Value = serde_json::from_str(body).unwrap();
        assert_eq!(data["userid"], 900000);
        assert_eq!(data["uuid"], "fixture-uuid");
        assert_eq!(data["type"], 1);
        assert!(data.get("token").is_none());
        assert!(data.get("d_sec").is_none());
    }

    #[tokio::test]
    #[cfg(feature = "local-api-fixtures")]
    async fn import_transport_signs_sampled_query_and_authenticates_only_in_body() {
        let fixture: serde_json::Value = serde_json::from_slice(include_bytes!(
            "../../../../tests/fixtures/api/import_playlist/link-add-qq-20261013-shaped.json"
        ))
        .unwrap();
        let body = serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap();
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut incoming = Vec::new();
            loop {
                let mut buffer = [0; 2048];
                let count = socket.read(&mut buffer).unwrap();
                assert!(count > 0);
                incoming.extend_from_slice(&buffer[..count]);
                if let Some(end) = incoming.windows(4).position(|v| v == b"\r\n\r\n") {
                    let headers = String::from_utf8_lossy(&incoming[..end]).to_ascii_lowercase();
                    let length: usize = headers
                        .lines()
                        .find_map(|l| l.strip_prefix("content-length: "))
                        .unwrap()
                        .parse()
                        .unwrap();
                    if incoming.len() >= end + 4 + length {
                        break;
                    }
                }
            }
            write!(
                socket,
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                body.len()
            )
            .unwrap();
            socket.write_all(&body).unwrap();
            String::from_utf8(incoming).unwrap()
        });
        let identity:Identity=serde_json::from_value(serde_json::json!({"userid":900000,"token":"fixture-secret","mid":"fixture-mid","dfid":"fixture-dfid","authentication":"current_account"})).unwrap();
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        crate::module::import_playlist::execute(
            &client,
            &identity,
            crate::module::import_playlist::Query::Link {
                url: "https://example.invalid/playlist".into(),
            },
        )
        .await
        .unwrap();
        let sent = server.join().unwrap();
        let (headers, body) = sent.split_once("\r\n\r\n").unwrap();
        let target = headers
            .lines()
            .next()
            .unwrap()
            .split_whitespace()
            .nth(1)
            .unwrap();
        let url = reqwest::Url::parse(&format!("http://localhost{target}")).unwrap();
        assert_eq!(url.path(), "/assetservice/import/v1/add_task");
        let mut params: BTreeMap<String, String> = url.query_pairs().into_owned().collect();
        assert_eq!(params.len(), 7);
        assert_eq!(params.get("appid").unwrap(), "3116");
        assert_eq!(params.get("clientver").unwrap(), "11440");
        assert_eq!(params.get("uuid").unwrap(), "-");
        assert!(
            !headers.contains("fixture-secret") && !headers.to_lowercase().contains("x-router:")
        );
        let signed = params.remove("signature").unwrap();
        assert_eq!(signed, signature::android(&params, body.as_bytes()));
        let data: serde_json::Value = serde_json::from_str(body).unwrap();
        assert_eq!(data["userid"], "900000");
        assert_eq!(data["token"], "fixture-secret");
        assert_eq!(data["source"], 3);
        assert_eq!(data["task_type"], 0);
        assert!(data.get("t1").is_none());
    }

    #[tokio::test]
    #[cfg(feature = "local-api-fixtures")]
    async fn anonymous_song_search_signs_explicit_zero_without_account_credentials() {
        use crate::module::search::{self, Query, SearchKind};
        // Serve a captured, redacted anonymous response. Assert the request at
        // the transport boundary, not a duplicate of the parameter constructor.
        let fixture: serde_json::Value = serde_json::from_slice(include_bytes!(
            "../../../../tests/fixtures/api/search/song-anonymous-explicit-userid.json"
        ))
        .unwrap();
        let body = serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap();
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut incoming = Vec::new();
            loop {
                let mut buffer = [0; 1024];
                let count = socket.read(&mut buffer).unwrap();
                if count == 0 {
                    break;
                }
                incoming.extend_from_slice(&buffer[..count]);
                if incoming.windows(4).any(|v| v == b"\r\n\r\n") {
                    break;
                }
            }
            write!(
                socket,
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                body.len()
            )
            .unwrap();
            socket.write_all(&body).unwrap();
            String::from_utf8(incoming).unwrap()
        });
        let identity: Identity = serde_json::from_value(serde_json::json!({
            "userid":123,"token":"fixture-secret","t1":"fixture-refresh",
            "mid":"fixture-mid","dfid":"fixture-dfid","authentication":"device_only"
        }))
        .unwrap();
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        let bytes = search::execute(
            &client,
            &identity,
            Query {
                keyword: "周杰伦".into(),
                kind: SearchKind::Song,
                page: 1,
                page_size: 30,
            },
        )
        .await
        .unwrap();
        let value: serde_json::Value = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(value["page"]["items"].as_array().unwrap().len(), 30);
        let sent = server.join().unwrap();
        assert!(sent.contains("userid=0") && sent.contains("signature="));
        for excluded in [
            "token=",
            "t1=",
            "userid=123",
            "fixture-secret",
            "fixture-refresh",
        ] {
            assert!(!sent.contains(excluded));
        }
    }

    #[tokio::test]
    async fn cloud_binary_transport_has_no_plaintext_account_or_android_signature() {
        // Local transport framing, not a sampled business response/converter.
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut incoming = Vec::new();
            loop {
                let mut buffer = [0; 1024];
                let count = socket.read(&mut buffer).unwrap();
                if count == 0 {
                    break;
                }
                incoming.extend_from_slice(&buffer[..count]);
                if incoming.ends_with(&[0, 255, 128]) {
                    break;
                }
            }
            socket
                .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}")
                .unwrap();
            incoming
        });
        let identity:Identity=serde_json::from_value(serde_json::json!({"userid":123,"token":"fixture-secret","t1":"fixture-refresh","mid":"fixture-mid","dfid":"fixture-dfid"})).unwrap();
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        client
            .request_cloud_with_metadata(
                &identity,
                Endpoint {
                    origin: Origin::Gateway,
                    path: "/fixture",
                    router: Some("cloudlist.service.kugou.com"),
                    method: Method::POST,
                    params: BTreeMap::from([
                        ("clienttime".into(), "1700000000".into()),
                        (
                            "key".into(),
                            super::super::cloudlist::keyed_time(1700000000),
                        ),
                        ("p".into(), "fixture-portrait".into()),
                    ]),
                    body: vec![0, 255, 128],
                    headers: &[("kg-rc", "1")],
                },
            )
            .await
            .unwrap();
        let incoming = server.join().unwrap();
        let request = String::from_utf8_lossy(&incoming);
        for excluded in [
            "token=",
            "userid=",
            "t1=",
            "uuid=",
            "signature=",
            "fixture-secret",
            "fixture-refresh",
        ] {
            assert!(!request.contains(excluded));
        }
        assert!(request.contains("key=bad0ee207bf429bd91402b77fc8f7a5b"));
        assert!(request.contains("p=fixture-portrait") && request.contains("kg-rc: 1"));
        assert!(incoming.ends_with(&[0, 255, 128]));
    }

    #[tokio::test]
    async fn device_only_excludes_credentials_and_http_error_preserves_ssa_event() {
        // Synthetic HTTP transport envelope, not an upstream business DTO sample.
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut incoming = Vec::new();
            loop {
                let mut buffer = [0; 1024];
                let count = socket.read(&mut buffer).unwrap();
                if count == 0 {
                    break;
                }
                incoming.extend_from_slice(&buffer[..count]);
                if incoming.windows(4).any(|v| v == b"\r\n\r\n") {
                    break;
                }
            }
            let body = r#"{"status":0,"error_code":20028}"#;
            write!(socket, "HTTP/1.1 403 Forbidden\r\nSSA-CODE: fixture-event\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{body}", body.len()).unwrap();
            String::from_utf8(incoming).unwrap()
        });
        let identity: Identity = serde_json::from_value(serde_json::json!({
            "userid":123,"token":"fixture-secret","t1":"fixture-refresh-secret",
            "mid":"fixture-mid","dfid":"fixture-dfid","authentication":"device_only"
        }))
        .unwrap();
        assert_eq!(identity.userid, 0);
        assert!(identity.token.is_empty() && identity.t1.is_empty());
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        let error = client
            .request(
                &identity,
                "/fixture",
                None,
                Method::GET,
                BTreeMap::new(),
                vec![],
            )
            .await
            .unwrap_err();
        assert_eq!(error.kind, "challenge");
        assert!(!format!("{error:?}").contains("fixture-event"));
        assert_eq!(error.challenge.unwrap().event_id, "fixture-event");
        let sent = server.join().unwrap();
        assert!(!sent.contains("token=") && !sent.contains("userid=") && !sent.contains("t1="));
        assert!(sent.contains("mid=fixture-mid") && sent.contains("dfid=fixture-dfid"));
        assert!(sent.contains("signature=")); // skip auth never disables signing.
    }

    #[test]
    fn current_account_retains_t1_and_full_endpoint_specific_device_snapshot() {
        let identity: Identity = serde_json::from_value(serde_json::json!({
            "userid":1,"token":"fixture-token","t1":"fixture-t1","mid":"fixture-mid","dfid":"fixture-dfid",
            "guid":"fixture-guid","mac":"fixture-mac","device_name":"fixture-device","uuid":"fixture-uuid","render_fingerprint":"123"
        })).unwrap();
        assert_eq!(identity.t1, "fixture-t1");
        assert_eq!(identity.guid, "fixture-guid");
        assert_eq!(identity.mac, "fixture-mac");
        assert_eq!(identity.device_name, "fixture-device");
        assert_eq!(identity.uuid, "fixture-uuid");
        assert_eq!(identity.render_fingerprint, "123");
    }
    #[tokio::test]
    async fn fingerprint_request_keeps_binary_body_signature_and_one_source_user_agent() {
        use crate::module::audio_match::{self, Query};
        use base64::Engine;
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let (mut socket, _) = listener.accept().unwrap();
            socket
                .set_read_timeout(Some(Duration::from_secs(5)))
                .unwrap();
            let mut incoming = Vec::new();
            loop {
                let mut chunk = [0; 4096];
                let n = socket.read(&mut chunk).unwrap();
                incoming.extend_from_slice(&chunk[..n]);
                if let Some(end) = incoming.windows(4).position(|v| v == b"\r\n\r\n") {
                    let header = String::from_utf8_lossy(&incoming[..end]).to_lowercase();
                    let count: usize = header
                        .lines()
                        .find_map(|v| v.strip_prefix("content-length: "))
                        .unwrap()
                        .parse()
                        .unwrap();
                    if incoming.len() >= end + 4 + count {
                        break;
                    }
                }
            }
            let response = r#"{"status":1,"error_code":0,"data":{}}"#;
            write!(
                socket,
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{response}",
                response.len()
            )
            .unwrap();
            incoming
        });
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        let identity:Identity=serde_json::from_value(serde_json::json!({"userid":123,"token":"fixture-token","mid":"fixture-mid","dfid":"fixture-dfid"})).unwrap();
        let pcm: Vec<u8> = (0..16000).map(|i| (i % 256) as u8).collect();
        let result = audio_match::execute(
            &client,
            Query {
                pcm_base64: base64::engine::general_purpose::STANDARD.encode(&pcm),
            },
            &identity,
        )
        .await
        .unwrap();
        assert_eq!(result, b"[]");
        let incoming = server.join().unwrap();
        let end = incoming.windows(4).position(|v| v == b"\r\n\r\n").unwrap();
        assert_eq!(&incoming[end + 4..], pcm);
        let headers = String::from_utf8_lossy(&incoming[..end]).to_lowercase();
        assert!(headers.starts_with("post /fingerprint.service/v1/music_trackid_mulit?"));
        assert!(
            headers.contains("useid=123")
                && headers.contains("multi_result=1")
                && headers.contains("signature=")
        );
        assert_eq!(headers.matches("user-agent:").count(), 1);
        assert!(headers.contains("user-agent: kugou/11490 (android)"));
        assert!(headers.contains("content-type: application/octet-stream"));
    }
    #[tokio::test]
    #[cfg(feature = "local-api-fixtures")]
    async fn song_barrage_resolves_hash_before_its_single_post() {
        use std::io::{Read, Write};
        let fixture: serde_json::Value = serde_json::from_slice(include_bytes!(
            "../../../../tests/fixtures/api/song_barrage/playback-1-20261019.json"
        ))
        .unwrap();
        let lookup = serde_json::to_vec(&fixture["responses"][0]["payload"]).unwrap();
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        listener.set_nonblocking(true).unwrap();
        let address = listener.local_addr().unwrap();
        let server = std::thread::spawn(move || {
            let mut requests = Vec::new();
            for body in [
                lookup,
                br#"{"status":0,"err_code":401,"message":"offline rejection"}"#.to_vec(),
            ] {
                let start = std::time::Instant::now();
                let mut socket = loop {
                    match listener.accept() {
                        Ok((socket, _)) => break socket,
                        Err(e)
                            if e.kind() == std::io::ErrorKind::WouldBlock
                                && start.elapsed().as_secs() < 5 =>
                        {
                            std::thread::sleep(std::time::Duration::from_millis(5))
                        }
                        other => panic!("Missing protocol request: {other:?}"),
                    }
                };
                socket
                    .set_read_timeout(Some(std::time::Duration::from_secs(5)))
                    .unwrap();
                let mut incoming = Vec::new();
                loop {
                    let mut bytes = [0; 4096];
                    let size = socket.read(&mut bytes).unwrap();
                    assert!(size > 0);
                    incoming.extend_from_slice(&bytes[..size]);
                    if let Some(end) = incoming.windows(4).position(|v| v == b"\r\n\r\n") {
                        let headers =
                            String::from_utf8_lossy(&incoming[..end]).to_ascii_lowercase();
                        let length = headers
                            .lines()
                            .find_map(|l| l.strip_prefix("content-length: "))
                            .unwrap_or("0")
                            .parse::<usize>()
                            .unwrap();
                        if incoming.len() >= end + 4 + length {
                            break;
                        }
                    }
                }
                write!(
                    socket,
                    "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                    body.len()
                )
                .unwrap();
                socket.write_all(&body).unwrap();
                requests.push(String::from_utf8(incoming).unwrap());
            }
            requests
        });
        let mut client = ApiClient::new().unwrap();
        client.base_url = format!("http://{address}");
        let identity: Identity = serde_json::from_value(serde_json::json!({"userid":42,"token":"offline-test-token","mid":"offline-test-mid","dfid":"-"})).unwrap();
        let result = crate::module::song_barrage_send::execute(
            &client,
            &identity,
            crate::module::song_barrage_send::Query {
                hash: "105A2705BEEE36A8BED029A8E1A6311D".into(),
                name: "龙战骑士".into(),
                content: "离线协议 🎵".into(),
            },
        )
        .await;
        assert!(result.is_err());
        let requests = server.join().unwrap();
        assert_eq!(requests.len(), 2);
        let query = |request: &str| {
            reqwest::Url::parse(&format!(
                "http://local{}",
                request
                    .lines()
                    .next()
                    .unwrap()
                    .split_whitespace()
                    .nth(1)
                    .unwrap()
            ))
            .unwrap()
            .query_pairs()
            .into_owned()
            .collect::<std::collections::BTreeMap<String, String>>()
        };
        assert!(requests[0].starts_with("GET "));
        let lookup = query(&requests[0]);
        assert_eq!(lookup["code"], "articulossong");
        assert_eq!(lookup["extdata"], "105A2705BEEE36A8BED029A8E1A6311D");
        assert_eq!(lookup["pagesize"], "1");
        assert_eq!(lookup["p"], "1");
        assert!(requests[1].starts_with("POST "));
        let sent = query(&requests[1]);
        assert_eq!(sent["code"], "articulossong");
        assert_eq!(sent["r"], "commentsv3/add");
        assert_eq!(sent["childrenid"], "154262");
        let body: serde_json::Value =
            serde_json::from_str(requests[1].split("\r\n\r\n").nth(1).unwrap()).unwrap();
        assert_eq!(body["data"]["content"], "离线协议 🎵");
        assert!(body["data"].get("album_audio_id").is_none());
        assert_eq!(body["data"]["images"], serde_json::json!([]));
    }
}
