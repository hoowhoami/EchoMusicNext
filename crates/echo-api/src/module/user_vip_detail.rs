//! Union VIP response is parsed independently of the profile response.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::{Deserialize, Serialize};
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct BusinessVip {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_vip: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    product_type: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    busi_type: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    vip_begin_time: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    vip_end_time: String,
}
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    vip_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    user_type: Option<u32>,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    vip_begin_time: String,
    #[serde(default)]
    #[serde(deserialize_with = "crate::util::response::text")]
    su_vip_begin_time: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    vip_end_time: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    su_vip_end_time: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    busi_vip: Vec<BusinessVip>,
}
#[derive(Serialize)]
pub struct MembershipProduct {
    pub active: Option<bool>,
    pub active_code: Option<u32>,
    pub product: String,
    pub business: String,
    /// ISO local date-time; provider response has no zone, so do not invent UTC.
    pub begins_at: Option<String>,
    pub expires_at: Option<String>,
}
#[derive(Serialize)]
pub struct Memberships {
    pub user_id: u64,
    pub vip_type_code: Option<u32>,
    pub user_type_code: Option<u32>,
    pub deluxe_begins_at: Option<String>,
    pub super_begins_at: Option<String>,
    pub deluxe_expires_at: Option<String>,
    pub super_expires_at: Option<String>,
    pub products: Vec<MembershipProduct>,
}
pub fn convert(bytes: &[u8]) -> Result<Memberships, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let value: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Memberships {
        user_id: value.userid,
        vip_type_code: value.vip_type,
        user_type_code: value.user_type,
        deluxe_begins_at: expiry(&value.vip_begin_time)?,
        super_begins_at: expiry(&value.su_vip_begin_time)?,
        deluxe_expires_at: expiry(&value.vip_end_time)?,
        super_expires_at: expiry(&value.su_vip_end_time)?,
        products: value
            .busi_vip
            .into_iter()
            .map(|v| {
                Ok(MembershipProduct {
                    active: v.is_vip.and_then(crate::util::numeric::boolean),
                    active_code: v.is_vip,
                    product: v.product_type,
                    business: v.busi_type,
                    begins_at: expiry(&v.vip_begin_time)?,
                    expires_at: expiry(&v.vip_end_time)?,
                })
            })
            .collect::<Result<_, ApiError>>()?,
    })
}
fn expiry(value: &str) -> Result<Option<String>, ApiError> {
    if value.is_empty() {
        return Ok(None);
    }
    let mut iso = value.to_owned();
    if value.is_ascii() && value.as_bytes().get(10) == Some(&b' ') {
        iso.replace_range(10..11, "T");
    }
    Ok(Some(iso))
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Vip,
                path: "/v1/get_union_vip",
                router: None,
                method: reqwest::Method::GET,
                params: std::collections::BTreeMap::from([("busi_type".into(), "concept".into())]),
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
