//! Multipart image upload only. Account mutation belongs to the caller.
use crate::{
    error::ApiError,
    util::{
        request::{self, ApiClient, Identity},
        user_protocol,
    },
};
use base64::{Engine, engine::general_purpose::STANDARD};
use serde::{Deserialize, Serialize};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub image_base64: String,
    pub local_date: String,
}
#[derive(Deserialize)]
struct UploadWire {
    #[serde(rename = "IsSuccess")]
    #[serde(deserialize_with = "crate::util::response::scalar")]
    success: bool,
    #[serde(rename = "FileName", default)]
    #[serde(deserialize_with = "crate::util::response::list")]
    files: Vec<String>,
}
#[derive(Serialize)]
pub struct Upload {
    pub file_name: String,
}
pub fn uploaded_file(bytes: &[u8]) -> Result<String, ApiError> {
    let wire: UploadWire = request::decode(bytes)?;
    if !wire.success {
        return Err(ApiError::new("business", "头像上传失败"));
    }
    wire.files
        .into_iter()
        .next()
        .filter(|s| !s.is_empty())
        .ok_or_else(ApiError::decode)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    user_protocol::require_account(identity)?;
    const LIMIT: usize = 8 * 1024 * 1024;
    if query.image_base64.len() > LIMIT.div_ceil(3) * 4 {
        return Err(ApiError::new("input", "头像图片不能超过 8 MB"));
    }
    let image = STANDARD
        .decode(query.image_base64)
        .map_err(|_| ApiError::new("input", "图片数据无效"))?;
    if image.is_empty() || image.len() > LIMIT {
        return Err(ApiError::new("input", "头像图片不能超过 8 MB"));
    }
    let (extension, mime) = if image.starts_with(b"\x89PNG\r\n\x1a\n") {
        ("png", "image/png")
    } else if image.starts_with(b"GIF87a") || image.starts_with(b"GIF89a") {
        ("gif", "image/gif")
    } else if image.starts_with(b"\xff\xd8\xff") {
        ("jpg", "image/jpeg")
    } else {
        return Err(ApiError::new("input", "请选择 JPEG、PNG 或 GIF 图片"));
    };
    let checksum = crate::util::image_upload::checksum(&query.local_date)?;
    // Base64-free multipart transport, with a boundary absent from this image.
    let mut boundary = String::from("EchoMusicAvatarBoundary");
    while image
        .windows(boundary.len())
        .any(|s| s == boundary.as_bytes())
    {
        boundary.push('x');
    }
    let mut body = format!("--{boundary}\r\nContent-Disposition: form-data; name=\"type\"\r\n\r\nkugouicon\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"md5\"\r\n\r\n{checksum}\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"avatar.{extension}\"\r\nContent-Type: {mime}\r\n\r\n").into_bytes();
    body.extend(image);
    body.extend(format!("\r\n--{boundary}--\r\n").as_bytes());
    let uploaded = client
        .upload_avatar(body, format!("multipart/form-data; boundary={boundary}"))
        .await?;
    let file = uploaded_file(&uploaded)?;
    request::encode(&Upload { file_name: file })
}
