//! Shared sound-catalog protocol. Market, artist and earphone sound rows
//! retain their own optional metadata.
use crate::{domain::Page, error::ApiError, util::request};
use serde::{Deserialize, Serialize};
#[derive(Serialize)]
pub struct Effect {
    pub id: u64,
    pub name: String,
    pub author: String,
    pub intro: String,
    pub icon_url: String,
    pub labels: Vec<String>,
    pub user_count: Option<u64>,
    pub source: &'static str,
    pub sound_urls: Vec<String>,
    pub vpf_urls: Vec<String>,
    pub file_size: Option<u64>,
    pub unavailable_reason: Option<&'static str>,
}
#[derive(Deserialize)]
pub struct Sound {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    pub id: u64,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub name: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub sound: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub sound_bk: Vec<String>,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub vpf: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub vpf_bk: Vec<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub privilege: Option<u32>,
}
#[derive(Deserialize)]
pub struct CatalogRow {
    #[serde(flatten)]
    pub sound: Sound,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub author: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub intro: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub icon_url: String,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub label: Vec<String>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub user_count: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub filesize: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub singer_privilege: Option<u32>,
}
#[derive(Deserialize)]
pub struct Headphone {
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub model: String,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    pub model_icon: String,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub is_unlocked: Option<u32>,
    pub sound: Sound,
}
fn image(s: String) -> String {
    crate::domain::cover_url(s)
}
fn urls(backups: Vec<String>, primary: String) -> Vec<String> {
    let mut out = Vec::new();
    for raw in backups.into_iter().chain([primary]) {
        if let Ok(mut u) = reqwest::Url::parse(&raw) {
            if u.scheme() == "http" {
                let _ = u.set_scheme("https");
            }
            if u.scheme() == "https" && u.username().is_empty() && u.password().is_none() {
                let value = u.to_string();
                if !out.contains(&value) {
                    out.push(value);
                }
            }
        }
    }
    out
}
fn base(sound: Sound, source: &'static str) -> Result<Effect, ApiError> {
    Ok(Effect {
        id: sound.id,
        name: sound.name,
        author: String::new(),
        intro: String::new(),
        icon_url: String::new(),
        labels: Vec::new(),
        user_count: None,
        source,
        sound_urls: urls(sound.sound_bk, sound.sound),
        vpf_urls: urls(sound.vpf_bk, sound.vpf),
        file_size: None,
        unavailable_reason: if sound.privilege.is_some_and(|v| v > 0) {
            Some("此音效需要上游授权，暂不支持下载")
        } else {
            None
        },
    })
}
pub fn catalog(row: CatalogRow, source: &'static str) -> Result<Effect, ApiError> {
    let mut e = base(row.sound, source)?;
    e.author = row.author;
    e.intro = row.intro;
    e.icon_url = image(row.icon_url);
    e.labels = row.label;
    e.user_count = row.user_count;
    e.file_size = row.filesize;
    if row.singer_privilege.is_some_and(|v| v > 0) {
        e.unavailable_reason = Some("此音效需要上游授权，暂不支持下载");
    }
    Ok(e)
}
pub fn headphone(row: Headphone) -> Result<Effect, ApiError> {
    let mut e = base(row.sound, "headphone")?;
    e.name = row.model;
    e.icon_url = image(row.model_icon);
    e.intro = "针对对应耳机型号调校，请选择与你的设备一致的型号。".into();
    if row.is_unlocked.is_some_and(|value| value != 1) {
        e.unavailable_reason = Some("此耳机音效尚未解锁");
    }
    Ok(e)
}
#[derive(Deserialize)]
pub struct CatalogWire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    pub status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    pub errcode: i64,
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    pub total: Option<u64>,
    #[serde(default, deserialize_with = "crate::util::response::list")]
    pub data: Vec<CatalogRow>,
}
pub fn catalog_page(
    bytes: &[u8],
    page: u32,
    size: u32,
    source: &'static str,
) -> Result<Page<Effect>, ApiError> {
    let wire: CatalogWire = request::decode_envelope(bytes, "errcode")?;
    request::success(wire.status, wire.errcode)?;
    Ok(Page {
        page: Some(page),
        page_size: Some(size),
        total: wire.total,
        items: wire
            .data
            .into_iter()
            .map(|v| catalog(v, source))
            .collect::<Result<_, _>>()?,
    })
}
pub fn paging(page: u32, size: u32) -> Result<(), ApiError> {
    if page == 0 || size == 0 || size > 50 {
        Err(ApiError::new("input", "音效分页无效"))
    } else {
        Ok(())
    }
}
#[cfg(all(test, feature = "local-api-fixtures"))]
mod tests {
    use super::*;
    #[test]
    fn actual_catalog_rows_and_earphone_sound_identity() {
        for (module, source) in [("effects_artist", "artist"), ("get_model", "market")] {
            let file = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join(format!(
                "../../tests/fixtures/api/{module}/first-page-playback-20261013.json"
            ));
            let sample: serde_json::Value =
                serde_json::from_slice(&std::fs::read(file).unwrap()).unwrap();
            let page = catalog_page(
                &serde_json::to_vec(&sample["responses"][0]["payload"]).unwrap(),
                1,
                20,
                source,
            )
            .unwrap();
            assert_eq!(page.items.len(), 20);
            assert!(page.items.iter().all(|e| e.source == source && e.id > 0));
        }
        let file = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../tests/fixtures/api/effects_brand_detail/brand-77-playback-20261013.json");
        let sample: serde_json::Value =
            serde_json::from_slice(&std::fs::read(file).unwrap()).unwrap();
        let raw = &sample["responses"][0]["payload"]["data"]["list"][0];
        let row: Headphone = serde_json::from_value(raw.clone()).unwrap();
        let effect = headphone(row).unwrap();
        assert_eq!(effect.id, 5753);
        assert_ne!(effect.id, raw["model_id"].as_u64().unwrap());
    }
}

#[cfg(test)]
mod admission_tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn optional_authorization_metadata_only_blocks_explicit_restrictions() {
        let row = json!({"id":17,"name":"known effect","sound":"https://example.test/effect.wav"});
        let effect = catalog(serde_json::from_value(row.clone()).unwrap(), "market").unwrap();
        assert!(effect.unavailable_reason.is_none());
        assert_eq!(effect.id, 17);
        assert_eq!(effect.sound_urls, ["https://example.test/effect.wav"]);
        for field in ["privilege", "singer_privilege"] {
            let mut denied = row.clone();
            denied[field] = json!(1);
            assert!(
                catalog(serde_json::from_value(denied).unwrap(), "market")
                    .unwrap()
                    .unavailable_reason
                    .is_some()
            );
        }
        for unlocked in [None, Some(1), Some(0), Some(2)] {
            let mut headphone_row = json!({"model":"known headphone","sound":row});
            if let Some(code) = unlocked {
                headphone_row["is_unlocked"] = json!(code);
            }
            let effect = headphone(serde_json::from_value(headphone_row).unwrap()).unwrap();
            assert_eq!(effect.id, 17);
            assert_eq!(
                effect.unavailable_reason.is_some(),
                unlocked.is_some_and(|code| code != 1)
            );
        }
    }
}
