//! Current account first-page visitor list. This uses clientver10672 and its
//! own signed query, not the default query injection or the relation DTO.
use crate::{
    domain::{ProfileIdentity, SocialIdentityFacts, SocialMembership, SocialPage, SocialUser},
    error::ApiError,
    util::{
        crypto,
        request::{self, ApiClient, Endpoint, Identity, Origin},
        signature, user_protocol,
    },
};
use serde::Deserialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct Row {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    userid: u64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    singerid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    nickname: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pic: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    source_desc: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    visit_time: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    singer_status: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    vip_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    user_type: Option<u32>,
}
#[derive(Deserialize)]
struct Data {
    #[serde(deserialize_with = "crate::util::response::list")]
    list: Vec<Row>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    next: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    hvisitors: Option<u64>,
}
pub fn convert(bytes: &[u8]) -> Result<SocialPage, ApiError> {
    let w: Wire = request::decode(bytes)?;
    request::success(w.status, w.error_code)?;
    let d: Data = crate::util::response::from_value(w.data).map_err(|_| ApiError::decode())?;
    let users = d
        .list
        .into_iter()
        .map(|r| {
            Ok(SocialUser {
                key: format!("user:{}", r.userid),
                user_id: r.userid,
                artist_id: r.singerid.filter(|v| *v > 0),
                nickname: r.nickname,
                avatar_url: r.pic,
                description: r.source_desc,
                grade: None,
                following: None,
                mutual: None,
                visited_at_seconds: r.visit_time,
                // Visitor wire has singer_status, not follow's is_star or
                // talent bitset. Do not copy a cross-endpoint fallback mapper.
                identity_facts: SocialIdentityFacts {
                    talent_bits: None,
                    star_code: None,
                    singer_status: r.singer_status,
                    vip_type: r.vip_type,
                    user_type: r.user_type,
                    identity_type: None,
                    friend_code: None,
                },
                identity: ProfileIdentity {
                    singer: r.singer_status == Some(1),
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
        total: d.total,
        next_page: d.next,
        visitor_count: d.hvisitors,
        list_revision: None,
        unmapped_entries: None,
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    user_protocol::require_account(identity)?;
    let time = request::unix_seconds()?;
    let p = crypto::user_portrait(&request::encode(
        &serde_json::json!({"clienttime":time,"token":identity.token,"t_userid":identity.userid}),
    )?)?;
    let body = request::encode(
        &serde_json::json!({"userid":identity.userid,"t_userid":identity.userid,"p":p,"page":1}),
    )?;
    let mut params = BTreeMap::from([
        ("dfid".into(), identity.dfid.clone()),
        ("plat".into(), "1".into()),
        ("clienttime".into(), time.to_string()),
        ("mid".into(), identity.mid.clone()),
        ("uuid".into(), "-".into()),
        ("clientver".into(), "10672".into()),
        ("appid".into(), "3116".into()),
    ]);
    params.insert("signature".into(), signature::android(&params, &body));
    let bytes = client
        .request_grade(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v2/get_visitors",
                router: Some("usercenter.kugou.com"),
                method: reqwest::Method::POST,
                params,
                body,
                headers: &[
                    ("Content-Type", "text/plain; charset=ISO-8859-1"),
                    ("User-Agent", "Android15-1070-10672-201-0-User-wifi"),
                    ("KG-RC", "1"),
                    ("KG-Rec", "1"),
                    ("KG-THash", "3980f3c"),
                ],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
