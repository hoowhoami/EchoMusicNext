//! /user/follow/add maps POST /v2/follow on relation.user.kugou.com. The sample
//! returns business error 31702 when the target is already followed; existing
//! follow/unfollow ACKs use status/error_code with data.is_friend.
use crate::{
    error::ApiError,
    util::{
        relation,
        request::{self, ApiClient, Endpoint, Identity},
    },
};
use serde::Serialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
#[derive(Debug, Serialize)]
pub struct FollowChange {
    pub following: bool,
    pub mutual: Option<bool>,
    pub already: bool,
}
fn flag(value: Option<u32>) -> Option<bool> {
    value.and_then(crate::util::numeric::boolean)
}
pub fn convert(bytes: &[u8]) -> Result<FollowChange, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status == 1 && wire.error_code == 0 {
        let is_friend = wire
            .data
            .get("is_friend")
            .and_then(serde_json::Value::as_u64)
            .and_then(|value| flag(Some(value as u32)));
        return Ok(FollowChange {
            following: true,
            mutual: is_friend,
            already: false,
        });
    }
    // Code 31702 means already followed. Settle on the followed state.
    if wire.error_code == 31702 {
        return Ok(FollowChange {
            following: true,
            mutual: None,
            already: true,
        });
    }
    Err(ApiError::business(wire.error_code))
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    target: u64,
) -> Result<Vec<u8>, ApiError> {
    relation::require_account(identity)?;
    let body = request::encode(&relation::body(identity, target)?)?;
    let headers = relation::headers(identity, "relation.user.kugou.com");
    let headers = headers
        .iter()
        .map(|(k, v)| (*k, v.as_str()))
        .collect::<Vec<_>>();
    let bytes = client
        .request_relation(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/v2/follow",
                router: None,
                method: reqwest::Method::POST,
                params: BTreeMap::from([("dfid".into(), identity.dfid.clone())]),
                body,
                headers: &headers,
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}
