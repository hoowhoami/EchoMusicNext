//! Cloud upload stages. Qt owns the queue and rate limiter; each stage is
//! cancellable and writes are never retried here. Keep at most one 1 MiB part
//! in memory, read on a blocking worker from the selected-file snapshot.
use crate::{
    error::ApiError,
    util::{
        cloudlist,
        crypto::PlaylistEnvelope,
        request::{self, ApiClient, Endpoint, Identity, Origin},
    },
};
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    io::{Read, Seek, SeekFrom},
    time::UNIX_EPOCH,
};
const BUCKET: &str = "musicclound";
pub const PART_BYTES: u64 = 1024 * 1024;
#[derive(Deserialize)]
#[serde(tag = "stage", rename_all = "snake_case", deny_unknown_fields)]
pub enum Query {
    Auth {
        hash: String,
    },
    Initiate {
        hash: String,
        ext: String,
        authorization: String,
    },
    Part {
        hash: String,
        transfer: Transfer,
        file: FileSnapshot,
        part: u32,
    },
    Complete {
        hash: String,
        transfer: Transfer,
        parts: u32,
    },
    Add {
        file: CloudFile,
    },
}
#[derive(Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Transfer {
    pub host: String,
    pub authorization: String,
    pub upload_id: String,
}
#[derive(Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FileSnapshot {
    pub path: String,
    pub size: u64,
    pub modified_ms: u64,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct CloudFile {
    pub name: String,
    pub ext: String,
    pub author_name: String,
    pub hash: String,
    pub hash_std: String,
    pub audio_id: u64,
    pub album_audio_id: u64,
    pub size: u64,
    pub timelen: u64,
}
#[derive(Default, Deserialize, Serialize)]
pub struct BssData {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub authorization: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub upload_id: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub external_host: String,
    #[serde(
        default,
        rename = "x-bss-hash",
        deserialize_with = "crate::util::response::text"
    )]
    pub hash: String,
    #[serde(
        default,
        rename = "x-bss-filename",
        deserialize_with = "crate::util::response::text"
    )]
    pub filename: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub partnumber: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub total_length: Option<u64>,
}
type Wire = crate::util::response::DefaultDataEnvelope<serde_json::Value>;
pub fn convert_bss(bytes: &[u8]) -> Result<BssData, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())
}
#[derive(Default, Deserialize, Serialize)]
pub struct AddData {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub list: Vec<AddedFile>,
    #[serde(flatten)]
    pub counters: super::user_cloud::CloudCounters,
}
#[derive(Deserialize, Serialize)]
pub struct AddedFile {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub kv_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub album_audio_id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub hash: String,
    #[serde(flatten)]
    pub metadata: BTreeMap<String, serde_json::Value>,
}
pub fn convert_add(bytes: &[u8]) -> Result<AddData, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.error_code == 20017 {
        return Err(ApiError::authentication(20017));
    }
    request::success(wire.status, wire.error_code)?;
    if wire.data.is_null() {
        return Ok(AddData::default());
    }
    crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())
}
pub(crate) fn validate_hash(hash: &str) -> Result<(), ApiError> {
    if hash.len() != 32 || !hash.bytes().all(|v| v.is_ascii_hexdigit()) {
        return Err(ApiError::new("input", "无效的音频指纹"));
    }
    Ok(())
}
fn params(identity: &Identity, hash: &str) -> Result<BTreeMap<String, String>, ApiError> {
    validate_hash(hash)?;
    Ok(BTreeMap::from([
        ("bucket".into(), BUCKET.into()),
        ("filename".into(), hash.to_lowercase()),
        ("version".into(), "11440".into()),
        ("userid".into(), identity.userid.to_string()),
        ("token".into(), identity.token.clone()),
        ("dfid".into(), identity.dfid.clone()),
        ("mid".into(), identity.mid.clone()),
        ("uuid".into(), identity.guid.clone()),
        ("appid".into(), "3116".into()),
        ("clientver".into(), "11440".into()),
        ("clienttime".into(), request::unix_seconds()?.to_string()),
    ]))
}
fn host(value: &str) -> Result<String, ApiError> {
    let value = if value.contains("://") {
        value.to_owned()
    } else {
        format!("http://{value}")
    };
    let url =
        reqwest::Url::parse(&value).map_err(|_| ApiError::new("decode", "无效的云盘上传地址"))?;
    if !matches!(url.scheme(), "http" | "https")
        || !url.host_str().is_some_and(|v| v.ends_with(".kugou.com"))
        || !url.username().is_empty()
        || url.password().is_some()
        || url.query().is_some()
        || url.fragment().is_some()
        || url.path() != "/"
    {
        return Err(ApiError::new("decode", "无效的云盘上传地址"));
    }
    Ok(value.trim_end_matches('/').to_owned())
}
fn add_transfer(
    params: &mut BTreeMap<String, String>,
    transfer: &Transfer,
) -> Result<String, ApiError> {
    if transfer.authorization.is_empty() || transfer.upload_id.is_empty() {
        return Err(ApiError::new("input", "上传会话不完整"));
    }
    params.insert("authorization".into(), transfer.authorization.clone());
    params.insert("upload_id".into(), transfer.upload_id.clone());
    host(&transfer.host)
}
fn read_part(file: FileSnapshot, part: u32) -> Result<Vec<u8>, ApiError> {
    if part == 0 || file.size == 0 || file.size > 100 * 1024 * 1024 {
        return Err(ApiError::new("input", "上传文件大小无效"));
    }
    let mut input =
        std::fs::File::open(file.path).map_err(|_| ApiError::new("file", "无法读取上传文件"))?;
    let check = |input: &std::fs::File| -> Result<(), ApiError> {
        let stat = input
            .metadata()
            .map_err(|_| ApiError::new("file", "无法读取上传文件信息"))?;
        let modified = stat
            .modified()
            .ok()
            .and_then(|v| v.duration_since(UNIX_EPOCH).ok())
            .map(|v| v.as_millis() as u64);
        if !stat.is_file() || stat.len() != file.size || modified != Some(file.modified_ms) {
            return Err(ApiError::new("file", "上传文件已变化，请重新选择"));
        }
        Ok(())
    };
    check(&input)?;
    let start = u64::from(part - 1) * PART_BYTES;
    if start >= file.size {
        return Err(ApiError::new("input", "无效的上传分片"));
    }
    let mut bytes = vec![0; (file.size - start).min(PART_BYTES) as usize];
    input
        .seek(SeekFrom::Start(start))
        .and_then(|_| input.read_exact(&mut bytes))
        .map_err(|_| ApiError::new("file", "读取上传分片失败"))?;
    check(&input)?;
    Ok(bytes)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    cloudlist::account(identity)?;
    if let Query::Add { file } = query {
        validate_hash(&file.hash)?;
        if !file.hash_std.is_empty() {
            validate_hash(&file.hash_std)?;
        }
        let mut value = serde_json::to_value(file).map_err(|_| ApiError::encoding())?;
        value["bitrate"] = serde_json::json!(4);
        let envelope = PlaylistEnvelope::new(
            &serde_json::json!({"data":[value],"list_ver":0}),
            identity.userid,
            &identity.token,
        )?;
        let time = request::unix_seconds()?;
        let bytes = client
            .request_mcloud(
                identity,
                Endpoint {
                    origin: Origin::McloudService,
                    path: "/v1/add_files",
                    router: None,
                    method: reqwest::Method::POST,
                    params: super::user_cloud::transport_params(
                        identity,
                        time,
                        envelope.portrait.to_uppercase(),
                    ),
                    body: envelope.body.clone(),
                    headers: &[
                        ("Content-Type", "application/x-www-form-urlencoded"),
                        ("kg-rc", "1"),
                        ("kg-rec", "1"),
                        ("kg-thash", "5d816a0"),
                        ("kg-rf", "B9EDA08A64250DEFFBCADDEE00F8F25F"),
                    ],
                },
            )
            .await?;
        let decrypted = match envelope.decrypt(&bytes) {
            Ok(v) => v,
            Err(e) => {
                if serde_json::from_slice::<serde_json::Value>(&bytes).is_ok() {
                    bytes
                } else {
                    return Err(e);
                }
            }
        };
        return request::encode(&convert_add(&decrypted)?);
    }
    let (url, method, mut p, authorization, body) = match query {
        Query::Auth { hash } => {
            use md5::{Digest, Md5};
            let mut p = params(identity, &hash)?;
            p.insert("method".into(), "POST".into());
            p.insert("loginType".into(), "1".into());
            p.insert("extranet".into(), "1".into());
            p.insert(
                "buVerifyCode".into(),
                format!("{:x}", Md5::digest(format!("3116{BUCKET}8ae10344e9738dcb"))),
            );
            (
                "https://gateway.kugou.com/bsstrackercdngz/v1/upload/auth".into(),
                reqwest::Method::GET,
                p,
                String::new(),
                Vec::new(),
            )
        }
        Query::Initiate {
            hash,
            ext,
            authorization,
        } => {
            if ext.is_empty()
                || !ext.bytes().all(|v| v.is_ascii_alphanumeric())
                || authorization.is_empty()
            {
                return Err(ApiError::new("input", "上传格式或授权无效"));
            }
            let mut p = params(identity, &hash)?;
            p.insert("ssl".into(), "1".into());
            p.insert("extendname".into(), ext);
            p.insert("authorization".into(), authorization.clone());
            (
                "http://bssulbig.kugou.com/v2/multipart/initiate/music".into(),
                reqwest::Method::POST,
                p,
                authorization,
                Vec::new(),
            )
        }
        Query::Part {
            hash,
            transfer,
            file,
            part,
        } => {
            let mut p = params(identity, &hash)?;
            let host = add_transfer(&mut p, &transfer)?;
            p.insert("partnumber".into(), part.to_string());
            p.insert("body_empty".into(), "1".into());
            let body = tokio::task::spawn_blocking(move || read_part(file, part))
                .await
                .map_err(|_| ApiError::new("internal", "读取上传文件失败"))??;
            (
                format!("{host}/v3/multipart/upload"),
                reqwest::Method::POST,
                p,
                transfer.authorization,
                body,
            )
        }
        Query::Complete {
            hash,
            transfer,
            parts,
        } => {
            if parts == 0 || parts > 100 {
                return Err(ApiError::new("input", "无效的上传分片数"));
            }
            let mut p = params(identity, &hash)?;
            let host = add_transfer(&mut p, &transfer)?;
            p.insert("partnumber".into(), parts.to_string());
            p.insert("md5".into(), hash.to_lowercase());
            p.insert("if_id3".into(), "1".into());
            (
                format!("{host}/v3/multipart/complete"),
                reqwest::Method::POST,
                p,
                transfer.authorization,
                Vec::new(),
            )
        }
        Query::Add { .. } => unreachable!(),
    };
    // Upstream signs the query with an empty body even for binary parts.
    p.insert("signature".into(), crate::util::signature::android(&p, &[]));
    let bytes = client
        .request_cloud_upload(&url, method, p, &authorization, body)
        .await?;
    request::encode(&convert_bss(&bytes)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[cfg(feature = "local-api-fixtures")]
    fn fixture(name: &str) -> serde_json::Value {
        serde_json::from_str(
            &std::fs::read_to_string(format!(
                "{}/../../tests/fixtures/api/user_cloud_upload/{name}-upload-20261018.json",
                env!("CARGO_MANIFEST_DIR")
            ))
            .unwrap(),
        )
        .unwrap()
    }
    #[test]
    #[cfg(feature = "local-api-fixtures")]
    fn captured_two_part_and_instant_protocol() {
        let full = fixture("two-parts");
        let responses = full["responses"].as_array().unwrap();
        assert_eq!(responses.len(), 6);
        assert!(
            !convert_bss(&serde_json::to_vec(&responses[0]["payload"]).unwrap())
                .unwrap()
                .authorization
                .is_empty()
        );
        assert!(
            !convert_bss(&serde_json::to_vec(&responses[1]["payload"]).unwrap())
                .unwrap()
                .upload_id
                .is_empty()
        );
        let first = convert_bss(&serde_json::to_vec(&responses[2]["payload"]).unwrap()).unwrap();
        assert_eq!(first.partnumber, Some(1));
        assert_eq!(first.total_length, Some(PART_BYTES));
        let second = convert_bss(&serde_json::to_vec(&responses[3]["payload"]).unwrap()).unwrap();
        assert_eq!(second.partnumber, Some(2));
        // BSS reports cumulative bytes, not the length of this last part.
        assert_eq!(second.total_length, Some(1152044));
        let add = convert_add(&serde_json::to_vec(&responses[5]["payload"]).unwrap()).unwrap();
        assert_eq!(add.list.len(), 1);
        assert!(add.list[0].kv_id.unwrap() > 0);
        assert_eq!(add.list[0].metadata["size"], 1152044);
        assert_eq!(add.list[0].metadata["timelen"], 6000);
        let instant = fixture("instant");
        let init =
            convert_bss(&serde_json::to_vec(&instant["responses"][1]["payload"]).unwrap()).unwrap();
        assert!(init.upload_id.is_empty());
        assert!(!init.hash.is_empty());
    }
    #[test]
    fn acknowledgement_does_not_require_optional_list_or_counters() {
        for bytes in [
            br#"{"status":1,"error_code":0}"#.as_slice(),
            br#"{"status":1,"error_code":0,"data":{"list":null}}"#.as_slice(),
        ] {
            let ack = convert_add(bytes).unwrap();
            assert!(ack.list.is_empty());
            assert!(ack.counters.list_count.is_none());
        }
        assert!(matches!(
            convert_bss(br#"{"status":0,"error_code":20017}"#),
            Err(ApiError {
                kind: "authentication",
                ..
            })
        ));
    }
    #[test]
    fn bounded_file_parts_reject_changed_snapshot() {
        let path = std::env::temp_dir().join(format!("echo-cloud-parts-{}", std::process::id()));
        let bytes = vec![7; PART_BYTES as usize + 13];
        std::fs::write(&path, &bytes).unwrap();
        let stat = std::fs::metadata(&path).unwrap();
        let file = FileSnapshot {
            path: path.to_str().unwrap().into(),
            size: stat.len(),
            modified_ms: stat
                .modified()
                .unwrap()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_millis() as u64,
        };
        assert_eq!(
            read_part(file.clone(), 1).unwrap().len(),
            PART_BYTES as usize
        );
        assert_eq!(read_part(file.clone(), 2).unwrap(), vec![7; 13]);
        assert!(read_part(file.clone(), 3).is_err());
        std::fs::write(&path, [1, 2, 3]).unwrap();
        assert!(read_part(file, 1).is_err());
        std::fs::remove_file(path).unwrap();
        assert!(host("https://kugou.com.attacker.invalid").is_err());
        assert!(host("https://user:password@x.kugou.com").is_err());
        assert!(host("https://bss.kugou.com/?token=x").is_err());
    }
}
