//! /register/dev acknowledgement can omit the assigned identity even on status=1.
//! Never invent identifiers or mark a device registered without its returned identity.
use crate::{
    domain::RegisteredDevice,
    error::ApiError,
    util::{
        crypto::PlaylistEnvelope,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
type Wire = crate::util::response::Envelope<serde_json::Value>;
pub fn convert(bytes: &[u8]) -> Result<RegisteredDevice, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    if wire.data.as_array().is_some_and(|v| v.is_empty())
        || wire
            .data
            .as_object()
            .is_some_and(|v| !v.contains_key("dfid"))
    {
        return Err(ApiError::new(
            "device_registration_unassigned",
            "设备服务未返回设备身份",
        ));
    }
    #[derive(Deserialize)]
    struct WireDevice {
        #[serde(deserialize_with = "crate::util::response::scalar")]
        dfid: String,
    }
    let data: WireDevice =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    if data.dfid.is_empty() || data.dfid == "-" || data.dfid.len() > 512 {
        return Err(ApiError::new("authentication", "服务未返回有效登录凭证"));
    }
    Ok(RegisteredDevice { dfid: data.dfid })
}
// Retain registration JSON field order before encryption and signing.
#[derive(Serialize)]
struct DeviceProfile<'a> {
    #[serde(rename = "availableRamSize")]
    available_ram_size: u64,
    #[serde(rename = "availableRomSize")]
    available_rom_size: u64,
    #[serde(rename = "availableSDSize")]
    available_s_d_size: u64,
    #[serde(rename = "basebandVer")]
    baseband_ver: &'a str,
    #[serde(rename = "batteryLevel")]
    battery_level: u32,
    #[serde(rename = "batteryStatus")]
    battery_status: u32,
    #[serde(rename = "brand")]
    brand: &'a str,
    #[serde(rename = "buildSerial")]
    build_serial: &'a str,
    #[serde(rename = "device")]
    device: &'a str,
    #[serde(rename = "imei")]
    imei: &'a str,
    #[serde(rename = "imsi")]
    imsi: &'a str,
    #[serde(rename = "manufacturer")]
    manufacturer: &'a str,
    #[serde(rename = "uuid")]
    uuid: &'a str,
    #[serde(rename = "accelerometer")]
    accelerometer: bool,
    #[serde(rename = "accelerometerValue")]
    accelerometer_value: &'a str,
    #[serde(rename = "gravity")]
    gravity: bool,
    #[serde(rename = "gravityValue")]
    gravity_value: &'a str,
    #[serde(rename = "gyroscope")]
    gyroscope: bool,
    #[serde(rename = "gyroscopeValue")]
    gyroscope_value: &'a str,
    #[serde(rename = "light")]
    light: bool,
    #[serde(rename = "lightValue")]
    light_value: &'a str,
    #[serde(rename = "magnetic")]
    magnetic: bool,
    #[serde(rename = "magneticValue")]
    magnetic_value: &'a str,
    #[serde(rename = "orientation")]
    orientation: bool,
    #[serde(rename = "orientationValue")]
    orientation_value: &'a str,
    #[serde(rename = "pressure")]
    pressure: bool,
    #[serde(rename = "pressureValue")]
    pressure_value: &'a str,
    #[serde(rename = "step_counter")]
    step_counter: bool,
    #[serde(rename = "step_counterValue")]
    step_counter_value: &'a str,
    #[serde(rename = "temperature")]
    temperature: bool,
    #[serde(rename = "temperatureValue")]
    temperature_value: &'a str,
}
impl<'a> DeviceProfile<'a> {
    fn new(guid: &'a str) -> Self {
        Self {
            available_ram_size: 4983533568,
            available_rom_size: 48114719,
            available_s_d_size: 48114717,
            baseband_ver: "",
            battery_level: 100,
            battery_status: 3,
            brand: "Redmi",
            build_serial: "unknown",
            device: "marble",
            imei: guid,
            imsi: "",
            manufacturer: "Xiaomi",
            uuid: guid,
            accelerometer: false,
            accelerometer_value: "",
            gravity: false,
            gravity_value: "",
            gyroscope: false,
            gyroscope_value: "",
            light: false,
            light_value: "",
            magnetic: false,
            magnetic_value: "",
            orientation: false,
            orientation_value: "",
            pressure: false,
            pressure_value: "",
            step_counter: false,
            step_counter_value: "",
            temperature: false,
            temperature_value: "",
        }
    }
}
pub async fn request_wire(
    client: &ApiClient,
    identity: &Identity,
) -> Result<(u16, Vec<u8>, std::collections::BTreeMap<String, String>), ApiError> {
    if identity.guid.is_empty() {
        return Err(ApiError::new("input", "设备 GUID 无效"));
    }
    let mut anonymous = identity.clone();
    anonymous.userid = 0;
    anonymous.token.clear();
    anonymous.t1.clear();
    let body = DeviceProfile::new(&anonymous.guid);
    let envelope = PlaylistEnvelope::registration(&body)?;
    let bytes = client
        .request_endpoint_with_metadata(
            &anonymous,
            Endpoint {
                origin: Origin::Registration,
                path: "/risk/v2/r_register_dev",
                router: None,
                method: reqwest::Method::POST,
                params: std::collections::BTreeMap::from([
                    ("part".into(), "1".into()),
                    ("platid".into(), "1".into()),
                    ("p".into(), envelope.portrait.clone()),
                ]),
                body: envelope.base64_body(),
                headers: &[
                    ("Content-Type", "application/x-www-form-urlencoded"),
                    ("Accept", "application/json, text/plain, */*"),
                    ("kg-rc", "1"),
                    ("kg-thash", "5d816a0"),
                    ("kg-rec", "1"),
                    ("kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"),
                ],
            },
        )
        .await?;
    let plain = envelope
        .decrypt(&bytes.bytes)
        .map_err(|_| ApiError::new("device_registration_decrypt", "无法解密设备服务返回的数据"))?;
    // The transport is encrypted binary, independently of its MIME label.
    // Record only the decrypted JSON through the existing redactor, so an
    // unassigned identity is distinguishable from damaged ciphertext in logs.
    crate::util::response_logging::record(&plain, bytes.http_status, "/register/dev:decrypted");
    Ok((bytes.http_status, plain, bytes.request_parameters))
}

pub async fn execute(client: &ApiClient, identity: &Identity) -> Result<Vec<u8>, ApiError> {
    request::encode(&convert(&request_wire(client, identity).await?.1)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn missing_assignment_remains_failure_instead_of_generic_decode() {
        for data in [serde_json::json!([]), serde_json::json!({})] {
            let bytes =
                serde_json::to_vec(&serde_json::json!({"status":1,"error_code":0,"data":data}))
                    .unwrap();
            assert_eq!(
                convert(&bytes).err().unwrap().kind,
                "device_registration_unassigned"
            );
        }
        let failure = br#"{"status":0,"error_code":123,"data":{}}"#;
        assert_eq!(convert(failure).err().unwrap().kind, "business");
    }
    #[test]
    fn request_json_and_aes_bytes_match_source_module_golden() {
        let vector: serde_json::Value = serde_json::from_str(include_str!(
            "../../../../tests/fixtures/protocol/register-dev-request-20261017.json"
        ))
        .unwrap();
        let body = DeviceProfile::new("0123456789abcdef0123456789abcdef");
        let plain = serde_json::to_vec(&body).unwrap();
        assert_eq!(plain, vector["plain_text"].as_str().unwrap().as_bytes());
        let envelope = PlaylistEnvelope::registration_with_seed(
            &body,
            vector["seed"].as_str().unwrap().to_owned(),
        )
        .unwrap();
        assert_eq!(
            envelope.base64_body(),
            vector["ciphertext_base64"].as_str().unwrap().as_bytes()
        );
        assert_eq!(envelope.decrypt(&envelope.body).unwrap(), plain);
    }
}
