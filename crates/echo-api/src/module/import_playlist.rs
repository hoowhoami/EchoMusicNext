//! Playlist import link/screenshot stages and task results. Asset-service IDs
//! are strings in status rows and numbers in acknowledgements and results.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::BTreeMap;

#[derive(Deserialize)]
#[serde(tag = "action", rename_all = "snake_case", deny_unknown_fields)]
pub enum Query {
    Link {
        url: String,
    },
    SubmitImage {
        task_sn: String,
        image_base64: String,
    },
    Screenshot {
        task_sn: String,
        cloud_list_id: u64,
        title: String,
    },
    Status {
        task_ids: Vec<u64>,
    },
    Result {
        cloud_list_id: u64,
        page: u32,
        page_size: u32,
    },
    Count,
}
#[derive(Deserialize)]
struct AssetWire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    errcode: i64,
    #[serde(default)]
    data: Value,
}
#[derive(Deserialize)]
struct CreatedWire {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    task_type: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<i32>,
}
#[derive(Deserialize)]
struct StatusWire {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<i32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songs_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    matched_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    imported_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    missed_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    msg: String,
    #[serde(rename = "type")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    task_type: Option<u32>,
}
#[derive(Deserialize)]
struct ResultWire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(default)]
    data: Value,
}
#[derive(Deserialize)]
struct ResultData {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    listid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    id: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    source: Option<u32>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    status: Option<i32>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    name: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    songs_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    imported_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    missed_num: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    missed: Vec<MissedWire>,
}
#[derive(Deserialize)]
struct MissedWire {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    audio_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    author_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    album_name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    reason: String,
}
#[derive(Deserialize)]
struct CountWire {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    userid: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    fail_count: Option<u64>,
}
#[derive(Serialize)]
pub struct ImportTask {
    pub task_id: Option<u64>,
    pub user_id: Option<u64>,
    pub cloud_list_id: Option<u64>,
    pub source: Option<u32>,
    pub task_type: Option<u32>,
    pub status: Option<i32>,
    pub title: String,
    pub total: Option<u64>,
    pub matched: Option<u64>,
    pub imported: Option<u64>,
    pub missed: Option<u64>,
    pub message: String,
}
#[derive(Serialize)]
pub struct MissedTrack {
    pub title: String,
    pub artist: String,
    pub album: String,
    pub reason: String,
}
#[derive(Serialize)]
pub struct ImportResult {
    pub task: ImportTask,
    pub missed_tracks: Vec<MissedTrack>,
    pub page: u32,
    pub page_size: u32,
}
fn asset(bytes: &[u8]) -> Result<Value, ApiError> {
    let wire: AssetWire = request::decode(bytes)?;
    request::success(wire.status, wire.errcode)?;
    Ok(wire.data)
}
pub fn convert_created(bytes: &[u8]) -> Result<ImportTask, ApiError> {
    let data: CreatedWire =
        crate::util::response::from_value(asset(bytes)?).map_err(|_| ApiError::decode())?;
    Ok(ImportTask {
        task_id: data.id,
        user_id: data.userid,
        cloud_list_id: data.listid,
        source: data.source,
        task_type: data.task_type,
        status: data.status,
        title: String::new(),
        total: None,
        matched: None,
        imported: None,
        missed: None,
        message: String::new(),
    })
}
pub fn convert_status(bytes: &[u8]) -> Result<Vec<ImportTask>, ApiError> {
    let data = asset(bytes)?;
    if data.is_null() {
        return Ok(Vec::new());
    }
    let rows: Vec<StatusWire> =
        crate::util::response::from_value(data).map_err(|_| ApiError::decode())?;

    rows.into_iter()
        .map(|data| {
            let id = data.id;
            Ok(ImportTask {
                task_id: id,
                user_id: data.userid,
                cloud_list_id: data.listid,
                source: data.source,
                task_type: data.task_type,
                status: data.status,
                title: data.name,
                total: data.songs_num,
                matched: data.matched_num,
                imported: data.imported_num,
                missed: data.missed_num,
                message: data.msg,
            })
        })
        .collect()
}
pub fn convert_result(bytes: &[u8], page: u32, size: u32) -> Result<ImportResult, ApiError> {
    let wire: ResultWire = request::decode(bytes)?;
    request::success(wire.status, wire.error_code)?;
    let data: ResultData =
        crate::util::response::from_value(wire.data).map_err(|_| ApiError::decode())?;
    Ok(ImportResult {
        task: ImportTask {
            task_id: data.id,
            user_id: data.userid,
            cloud_list_id: data.listid,
            source: data.source,
            task_type: None,
            status: data.status,
            title: data.name,
            total: data.songs_num,
            matched: None,
            imported: data.imported_num,
            missed: data.missed_num,
            message: String::new(),
        },
        missed_tracks: data
            .missed
            .into_iter()
            .map(|v| MissedTrack {
                title: v.audio_name,
                artist: v.author_name,
                album: v.album_name,
                reason: v.reason,
            })
            .collect(),
        page,
        page_size: size,
    })
}
pub fn convert_upload(bytes: &[u8]) -> Result<Value, ApiError> {
    asset(bytes)?;
    // submit_img returns a string acknowledgement, not a task or OCR result.
    Ok(json!({"uploaded":true}))
}
pub fn convert_count(bytes: &[u8]) -> Result<Value, ApiError> {
    let data: CountWire =
        crate::util::response::from_value(asset(bytes)?).map_err(|_| ApiError::decode())?;
    Ok(json!({"user_id":data.userid,"count":data.count,"failed_count":data.fail_count}))
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if identity.userid == 0 || identity.token.is_empty() {
        return Err(ApiError::new("authentication", "请先登录"));
    }
    let mut body = json!({"userid":identity.userid.to_string(),"token":identity.token});
    let path = match &query {
        Query::Link { url } => {
            if url.len() > 8192 || !url.contains("https://") {
                return Err(ApiError::new("input", "请输入歌单分享链接"));
            }
            body["source"] = 3.into();
            body["task_type"] = 0.into();
            body["url"] = url.clone().into();
            "/assetservice/import/v1/add_task"
        }
        Query::SubmitImage {
            task_sn,
            image_base64,
        } => {
            if task_sn.is_empty()
                || task_sn.len() > 128
                || image_base64.is_empty()
                || image_base64.len() > 14 * 1024 * 1024
            {
                return Err(ApiError::new("input", "截图数据无效"));
            }
            body["task_sn"] = task_sn.clone().into();
            body["img_base64"] = image_base64.clone().into();
            "/assetservice/import/v1/submit_img"
        }
        Query::Screenshot {
            task_sn,
            cloud_list_id,
            title,
        } => {
            if task_sn.is_empty()
                || task_sn.len() > 128
                || *cloud_list_id == 0
                || title.trim().is_empty()
                || title.len() > 1024
            {
                return Err(ApiError::new("input", "请选择或创建目标歌单"));
            }
            body["source"] = 3.into();
            body["task_type"] = 1.into();
            body["task_sn"] = task_sn.clone().into();
            body["listid"] = (*cloud_list_id).into();
            body["list_name"] = title.clone().into();
            "/assetservice/import/v1/add_task"
        }
        Query::Status { task_ids } => {
            if task_ids.is_empty() || task_ids.len() > 100 || task_ids.contains(&0) {
                return Err(ApiError::new("input", "导入任务标识无效"));
            }
            body["ids"] = json!(task_ids);
            "/assetservice/import/v1/query_task_status"
        }
        Query::Result {
            cloud_list_id,
            page,
            page_size,
        } => {
            if *cloud_list_id == 0 || *page == 0 || !(1..=100).contains(page_size) {
                return Err(ApiError::new("input", "导入明细分页无效"));
            }
            body["listid"] = cloud_list_id.to_string().into();
            body["page"] = (*page).into();
            body["pagesize"] = (*page_size).into();
            body["show_missed"] = 1.into();
            "/pubsongs/v1/query_task"
        }
        Query::Count => {
            body["classify"] = 1.into();
            "/assetservice/import/v1/task_count"
        }
    };
    let params = BTreeMap::from([
        ("appid".into(), "3116".into()),
        ("clientver".into(), "11440".into()),
        ("clienttime".into(), request::unix_seconds()?.to_string()),
        ("mid".into(), identity.mid.clone()),
        ("uuid".into(), "-".into()),
        ("dfid".into(), identity.dfid.clone()),
    ]);
    let bytes = client
        .request_import(
            identity,
            Endpoint {
                origin: Origin::Gateway,
                path,
                router: None,
                method: reqwest::Method::POST,
                params,
                body: request::encode(&body)?,
                headers: &[],
            },
        )
        .await?;
    match query {
        Query::Link { .. } => {
            let task = convert_created(&bytes)?;
            request::encode(&task)
        }
        Query::Screenshot { .. } => {
            let task = convert_created(&bytes)?;
            request::encode(&task)
        }
        Query::SubmitImage { .. } => request::encode(&convert_upload(&bytes)?),
        Query::Status { .. } => request::encode(&convert_status(&bytes)?),
        Query::Result {
            page, page_size, ..
        } => request::encode(&convert_result(&bytes, page, page_size)?),
        Query::Count => request::encode(&convert_count(&bytes)?),
    }
}
