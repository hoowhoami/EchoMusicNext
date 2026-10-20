//! /user/detail account ID comes from the caller context.
use crate::{
    domain::{Profile, ProfileIdentity},
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Identity},
    },
};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    gender: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    vip_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    follows: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fans: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    descri: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    friends: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    p_grade: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    duration: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hvisitors: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    rtime: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    province: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    city: String,
    // loc is IP-region display text, separate from editable province/city preferences.
    #[serde(default, deserialize_with = "crate::util::response::text")]
    loc: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    birthday: String,
    // Preserve the independent profile fields when present.
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kq_talent: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    actor_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    biz_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    tme_star_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    singer_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_star: Option<i32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    auth_info: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    auth_info_talent: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    auth_info_singer: String,
}
#[derive(Serialize)]
struct Portrait<'a> {
    token: &'a str,
    clienttime: u64,
}
#[derive(Serialize)]
struct Body {
    visit_time: u64,
    usertype: u32,
    p: String,
    userid: u64,
}
pub fn convert(bytes: &[u8], user_id: u64) -> Result<Profile, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    // The real invalid-token response has data:null. Check its status before
    // decoding a success Data; otherwise authentication becomes a contract error.
    if wire.status == 0 && wire.error_code == 20018 {
        return Err(ApiError::authentication(wire.error_code));
    }
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(Profile {
        id: user_id,
        nickname: data.nickname,
        avatar_url: data.pic,
        gender: data.gender,
        vip_type: data.vip_type,
        follows: data.follows,
        fans: data.fans,
        biography: data.descri,
        friends: data.friends,
        grade: data.p_grade,
        listening_minutes: data.duration,
        visitors: data.hvisitors,
        registered_at_seconds: data.rtime,
        province: data.province,
        city: data.city,
        ip_location: data.loc,
        birthday: data.birthday,
        identity: ProfileIdentity {
            actor_code: data.actor_status,
            business_code: data.biz_status,
            star_code: data.tme_star_status,
            singer_code: data.singer_status,
            legacy_star_code: data.is_star,
            talent_bits: data.kq_talent,
            actor: data.actor_status == Some(1),
            business: data.biz_status == Some(1),
            star: data.tme_star_status == Some(1),
            singer: data.singer_status == Some(1) || data.is_star == Some(1),
            authorizations: super::user_info::identity_labels(&data.auth_info),
            talent_authorizations: super::user_info::identity_labels(&data.auth_info_talent),
            singer_authorizations: super::user_info::identity_labels(&data.auth_info_singer),
        },
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    let timestamp = request::unix_seconds()?;
    let portrait = request::encode(&Portrait {
        token: &identity.token,
        clienttime: timestamp,
    })?;
    let body = request::encode(&Body {
        visit_time: timestamp,
        usertype: 1,
        p: crypto::user_portrait(&portrait)?,
        userid: identity.userid,
    })?;
    let bytes = client
        .request(
            identity,
            "/v3/get_my_info",
            Some("usercenter.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::from([("plat".into(), "1".into())]),
            body,
        )
        .await?;
    request::encode(&convert(&bytes, identity.userid)?)
}
