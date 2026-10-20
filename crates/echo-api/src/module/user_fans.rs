//! Current-account live list, with an explicit endpoint-specific wire DTO.
use crate::{
    domain::{ProfileIdentity, SocialIdentityFacts, SocialMembership, SocialPage, SocialUser},
    error::ApiError,
    util::{
        request::{self, ApiClient, Endpoint, Identity, Origin},
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
    kq_talent: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    is_star: Option<i32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    vip_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    user_type: Option<u32>,
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
        .map(|r| {
            Ok(SocialUser {
                key: format!("user:{}", r.userid),
                user_id: r.userid,
                artist_id: None,
                nickname: r.nickname,
                avatar_url: r.pic,
                description: r.source_desc,
                grade: r.grade,
                following: r.is_friend.and_then(crate::util::numeric::boolean),
                mutual: r.is_friend.and_then(crate::util::numeric::boolean),
                visited_at_seconds: None,
                identity_facts: SocialIdentityFacts {
                    talent_bits: r.kq_talent,
                    star_code: r.is_star,
                    singer_status: None,
                    vip_type: r.vip_type,
                    user_type: r.user_type,
                    identity_type: None,
                    friend_code: r.is_friend,
                },
                identity: ProfileIdentity {
                    talent_bits: r.kq_talent,
                    singer: r.is_star == Some(1),
                    ..Default::default()
                },
                membership: SocialMembership {
                    super_active: r.user_type.is_some_and(|v| v & 16 != 0),
                    deluxe_active: r.vip_type == Some(6),
                },
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
    let body = user_protocol::body(identity, request::unix_seconds()?)?;
    let bytes = client
        .request_grade(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v2/fans_list",
                router: Some("relationuser.kugou.com"),
                method: reqwest::Method::POST,
                params: BTreeMap::from([("dfid".into(), identity.dfid.clone())]),
                body: request::encode(&body)?,
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
