//! Current-account live list, with an explicit endpoint-specific wire DTO.
use crate::{
    domain::{ProfileIdentity, SocialIdentityFacts, SocialMembership, SocialPage, SocialUser},
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Identity},
        user_protocol,
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Row {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    source_desc: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    grade: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_friend: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    singerid: Option<u64>,
    // Artist-only rows can omit these facts. Preserve missing/unknown codes
    // separately from the identity labels projected for display.
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kq_talent: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_star: Option<i32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    vip_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    user_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    iden_type: Option<u32>,
}
#[derive(Deserialize)]
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(deserialize_with = "crate::util::response::list")]
    lists: Vec<Row>,
}
pub fn convert(bytes: &[u8]) -> Result<SocialPage, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let users = data
        .lists
        .into_iter()
        .enumerate()
        .map(|(index, r)| {
            let artist_id = r.singerid.filter(|id| *id > 0);
            let (talent_bits, singer, membership) = if r.userid > 0 {
                (
                    r.kq_talent,
                    r.is_star.unwrap_or_default() == 1,
                    SocialMembership {
                        super_active: r.user_type.unwrap_or_default() & 16 != 0,
                        deluxe_active: r.vip_type.unwrap_or_default() == 6,
                    },
                )
            } else {
                // The reference explicitly labels artist-only follow entries
                // as singers without manufacturing an account ID.
                (None, true, SocialMembership::default())
            };
            Ok(SocialUser {
                key: if r.userid > 0 {
                    format!("user:{}", r.userid)
                } else {
                    artist_id
                        .map(|id| format!("artist:{id}"))
                        .unwrap_or_else(|| format!("unidentified:{index}"))
                },
                user_id: r.userid,
                artist_id,
                nickname: r.nickname,
                avatar_url: r.pic,
                description: r.source_desc,
                grade: r.grade,
                following: if r.userid > 0 { Some(true) } else { None },
                mutual: r.is_friend.and_then(crate::util::numeric::boolean),
                visited_at_seconds: None,
                identity_facts: SocialIdentityFacts {
                    talent_bits: r.kq_talent,
                    star_code: r.is_star,
                    singer_status: None,
                    vip_type: r.vip_type,
                    user_type: r.user_type,
                    friend_code: r.is_friend,
                    identity_type: r.iden_type,
                },
                identity: ProfileIdentity {
                    talent_bits,
                    singer,
                    ..Default::default()
                },
                membership,
            })
        })
        .collect::<Result<Vec<_>, ApiError>>()?;
    Ok(SocialPage {
        users,
        total: data.total,
        next_page: None,
        visitor_count: None,
        list_revision: None,
        unmapped_entries: None,
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    user_protocol::require_account(identity)?;
    let time = request::unix_seconds()?;
    let p = crypto::user_portrait(&request::encode(
        &serde_json::json!({"clienttime":time,"token":identity.token}),
    )?)?;
    let body = request::encode(
        &serde_json::json!({"merge":2,"need_iden_type":1,"ext_params":"k_pic,jumptype,singerid,score","userid":identity.userid,"type":0,"id_type":0,"p":p}),
    )?;
    let bytes = client
        .request(
            identity,
            "/v4/follow_list",
            Some("relationuser.kugou.com"),
            reqwest::Method::POST,
            BTreeMap::from([("plat".into(), "1".into())]),
            body,
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
