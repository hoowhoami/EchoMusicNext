//! Discovery proxy: first ranked batch, then bounded listen batches when full is requested.
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
    discovery_cards::recommend(client, identity, query).await
}
