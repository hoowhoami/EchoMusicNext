//! The only observed friends success is total=0/lists={}. Nonempty is pending
//! sampling; retain unmodeled entries separately without rejecting the response.
use crate::{
    domain::SocialPage,
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
struct Data {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    total: Option<u64>,
    lists: serde_json::Value,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    list_ver: u64,
}
pub fn convert(bytes: &[u8]) -> Result<SocialPage, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: Data =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    let empty = data.lists.is_null()
        || data.lists.as_object().is_some_and(|v| v.is_empty())
        || data.lists.as_array().is_some_and(|v| v.is_empty());
    Ok(SocialPage {
        users: vec![],
        total: data.total,
        next_page: None,
        visitor_count: None,
        list_revision: Some(data.list_ver),
        unmapped_entries: if empty { None } else { Some(data.lists) },
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    let body = user_protocol::body(identity, request::unix_seconds()?)?;
    let bytes = client
        .request_grade(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path: "/v1/friends_list",
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
