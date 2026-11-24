//! Concept discovery single route; protocol and domain conversion are shared explicitly.
use crate::{
    error::ApiError,
    util::{
        discovery_cards::{self, Query},
        request::{ApiClient, Identity},
    },
};
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    discovery_cards::single(client, identity, query, None).await
}
