//! /user/follow/del maps POST /v1/unfollow on relationuser.kugou.com.
//! The acknowledgement uses data: true with status=1/error_code=0.
use crate::{
    error::ApiError,
    util::{
        relation,
        request::{self, ApiClient, Endpoint, Identity},
    },
};
use serde::Serialize;
use std::collections::BTreeMap;
type Wire = crate::util::response::AckEnvelope;
#[derive(Debug, Serialize)]
pub struct FollowChange {
    pub following: bool,
    pub mutual: Option<bool>,
    pub already: bool,
}
pub fn convert(bytes: &[u8]) -> Result<FollowChange, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    Ok(FollowChange {
        following: false,
        mutual: None,
        already: false,
    })
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    target: u64,
) -> Result<Vec<u8>, ApiError> {
    relation::require_account(identity)?;
    let body = request::encode(&relation::body(identity, target)?)?;
    let headers = relation::headers(identity, "relationuser.kugou.com");
    let headers = headers
        .iter()
        .map(|(k, v)| (*k, v.as_str()))
        .collect::<Vec<_>>();
    let bytes = client
        .request_relation(
            identity,
            Endpoint {
                origin: request::Origin::Gateway,
                path: "/v1/unfollow",
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
