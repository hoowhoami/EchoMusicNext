//! /login/qr/key. Real device-only sample: qrcode key and PNG data URL.
use crate::{
    domain::QrLoginSession,
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::Deserialize;
type Wire = crate::util::response::Envelope<serde_json::Value>;
#[derive(Deserialize)]
struct WireSession {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    qrcode: String,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    qrcode_img: String,
}
pub fn convert(bytes: &[u8]) -> Result<QrLoginSession, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: WireSession =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(QrLoginSession {
        key: data.qrcode,
        image_data_url: data.qrcode_img,
    })
}
pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    let mut anonymous = identity.clone();
    anonymous.userid = 0;
    anonymous.token.clear();
    anonymous.t1.clear();
    request::encode(&convert(
        &client
            .request_endpoint(
                &anonymous,
                Endpoint {
                    origin: Origin::QrLogin,
                    path: "/v2/qrcode",
                    router: None,
                    method: reqwest::Method::GET,
                    params: std::collections::BTreeMap::from([
                        ("appid".into(), "1001".into()),
                        ("type".into(), "1".into()),
                        ("plat".into(), "4".into()),
                        (
                            "qrcode_txt".into(),
                            "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=3116&"
                                .into(),
                        ),
                        ("srcappid".into(), "2919".into()),
                    ]),
                    body: vec![],
                    headers: &[],
                },
            )
            .await?,
    )?)
}
