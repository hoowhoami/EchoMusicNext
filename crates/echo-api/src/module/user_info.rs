//! Identity presentation whitelist from a real /v1/get_my_userinfo response.
//! Contact details and linked-provider secrets are deliberately discarded.
use crate::{
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Identity},
    },
};
use md5::{Digest, Md5};
use serde::{Deserialize, Serialize};
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    student_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    student_school: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    student_expire_time: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    tags: String,
}
#[derive(Serialize)]
pub struct UserIdentity {
    pub student_status: Option<u32>,
    pub student_school: String,
    pub student_expire_time: String,
    pub tags: Vec<String>,
}
/// Text labels are strings. Display formatting separates and deduplicates labels.
pub(crate) fn identity_labels(text: &str) -> Vec<String> {
    let mut labels = Vec::new();
    for part in text.split([',', '，', '、', ';', '；', '\n']) {
        let label = part.trim();
        if !label.is_empty() && !labels.iter().any(|existing| existing == label) {
            labels.push(label.to_owned());
        }
    }
    labels
}
pub fn convert(bytes: &[u8]) -> Result<UserIdentity, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(UserIdentity {
        student_status: data.student_status,
        student_school: data.student_school,
        student_expire_time: data.student_expire_time,
        tags: identity_labels(&data.tags),
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    let time = request::unix_milliseconds()?;
    let p = crypto::user_portrait(&request::encode(
        &serde_json::json!({"clienttime":time,"token":identity.token}),
    )?)?;
    let key = format!(
        "{:x}",
        Md5::digest(format!("3116LnT6xpN3khm36zse0QzvmgTZ3waWdRSA11440{time}").as_bytes())
    );
    let body = serde_json::json!({"p":p,"appid":3116,"mid":identity.mid,"clientver":11440,"source":0,"clienttime":time,"uuid":"-","userid":identity.userid,"key":key});
    let bytes = client
        .request(
            identity,
            "/v1/get_my_userinfo",
            Some("relation.user.kugou.com"),
            reqwest::Method::POST,
            Default::default(),
            request::encode(&body)?,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}

#[cfg(test)]
mod tests {
    #[test]
    fn label_presentation_preserves_order_and_removes_empty_duplicates() {
        // Product text projection only; this is not a provider response fixture.
        assert_eq!(
            super::identity_labels(" 摇滚，旅行、摇滚; English 中文；\n  "),
            ["摇滚", "旅行", "English 中文"]
        );
    }
}
