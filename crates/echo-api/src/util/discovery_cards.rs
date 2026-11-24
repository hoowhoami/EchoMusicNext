//! Concept Android 5.3.21 discovery protocol. Sign the exact serialized body;
//! each proxy child has its own query signature and shares the batch timestamp.
use crate::{
    domain::discovery::DiscoverySong,
    error::ApiError,
    module::ai_recommend_song,
    util::{
        request::{self, ApiClient, Identity},
        signature,
    },
};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
const CARDS: &[(u32, &str)] = &[
    (3007, "y_song_module_7"),
    (3032, "y_song_module_32"),
    (3014, "y_song_module_14"),
    (3101, "y_hot"),
    (3001, "y_song_module_1"),
    (3006, "y_song_module_6"),
    (3102, "y_tag"),
    (3004, "y_song_module_4"),
    (3005, "y_song_module_5"),
    (3103, "y_style"),
    (3107, "y_aisong"),
];
#[derive(Deserialize, Default)]
#[serde(default)]
pub struct Query {
    pub card_id: u32,
    pub cards: Option<Value>,
    pub card_size: Option<u32>,
    pub endpoint: String,
    pub full: bool,
    pub parse: bool,
    pub source_mixsong: String,
    pub tagid: String,
    pub u_info: String,
    pub page: Option<u32>,
    pub pagesize: Option<u32>,
    pub ufeature: Vec<String>,
}
#[derive(Serialize)]
pub struct SubRequest {
    pub url: String,
    pub body: String,
    pub method: String,
    pub headers: String,
    pub module: String,
}
pub fn module(id: u32) -> Option<&'static str> {
    CARDS.iter().find(|(n, _)| *n == id).map(|(_, s)| *s)
}
fn common(identity: &Identity, time: u64) -> BTreeMap<String, String> {
    BTreeMap::from([
        ("area_code".into(), "1".into()),
        ("mid".into(), identity.mid.clone()),
        ("userid".into(), identity.userid.to_string()),
        ("uuid".into(), "-".into()),
        ("platform".into(), "android".into()),
        ("token".into(), identity.token.clone()),
        ("dfid".into(), identity.dfid.clone()),
        ("appid".into(), "3116".into()),
        ("clientver".into(), "11621".into()),
        ("clienttime".into(), time.to_string()),
    ])
}
/// Exact request recipe used both for direct requests and proxy children.
pub fn build(id: u32, q: &Query, identity: &Identity, time: u64) -> Result<SubRequest, ApiError> {
    let module = module(id).ok_or_else(|| ApiError::new("input", "未知推荐卡片"))?;
    let mut params = common(identity, time);
    let method = "POST";
    let (path, body) = match id {
        3103 => {
            params.insert("ver".into(), "v2".into());
            params.insert("module_id".into(), "1".into());
            (
                "/youth/v1/song/tag_card_recommend",
                Some(json!({"source_mixsong":q.source_mixsong,"tagid":q.tagid,"u_info":q.u_info})),
            )
        }
        3107 => (
            "/concepts/v1/ai/recommend_song",
            Some(json!({"page":q.page.unwrap_or(1),"pagesize":q.pagesize.unwrap_or(6),"source":1})),
        ),
        _ => {
            params.insert("ver".into(), "v2".into());
            params.insert("module_id".into(), "1".into());
            params.insert("card_id".into(), id.to_string());
            params.insert(
                "pagesize".into(),
                q.pagesize
                    .unwrap_or(if id == 3004 || id == 3006 { 10 } else { 30 })
                    .to_string(),
            );
            (
                "/youth/v1/song/single_card_recommend",
                Some(json!({"source_mixsong":q.source_mixsong,"tagid":q.tagid,"u_info":q.u_info})),
            )
        }
    };
    let body = body
        .map(|v| serde_json::to_string(&v))
        .transpose()
        .map_err(|_| ApiError::encoding())?
        .unwrap_or_default();
    params.insert(
        "signature".into(),
        signature::android(&params, body.as_bytes()),
    );
    let mut url = reqwest::Url::parse(&format!("https://gateway.kugou.com{path}"))
        .map_err(|_| ApiError::encoding())?;
    url.query_pairs_mut().extend_pairs(params);
    Ok(SubRequest {
        url: url.into(),
        body,
        method: method.into(),
        headers: String::new(),
        module: module.into(),
    })
}
async fn send(
    client: &ApiClient,
    identity: &Identity,
    path: &str,
    params: BTreeMap<String, String>,
    body: Vec<u8>,
    method: reqwest::Method,
) -> Result<Vec<u8>, ApiError> {
    client
        .request_discovery(
            identity,
            request::Endpoint {
                origin: request::Origin::Gateway,
                path,
                router: None,
                method,
                params,
                body,
                headers: &[
                    (
                        "User-Agent",
                        "Android16-1070-11621-130-0-DiscoveryCard-wifi",
                    ),
                    ("Content-Type", "application/json; charset=utf-8"),
                ],
            },
        )
        .await
}
fn data(bytes: &[u8]) -> Result<Value, ApiError> {
    let v: Value = request::decode(bytes)?;
    request::success(
        v["status"].as_u64().unwrap_or(0) as u32,
        v["error_code"]
            .as_i64()
            .or(v["errcode"].as_i64())
            .unwrap_or(0),
    )?;
    Ok(v.get("data").cloned().unwrap_or(Value::Null))
}
#[derive(Serialize)]
pub struct Card {
    pub id: u32,
    pub title: String,
    pub description: String,
    pub songs: Vec<DiscoverySong>,
    pub items: Vec<Value>,
    pub groups: Vec<Value>,
    pub count: Option<u64>,
    pub provider_extension: Value,
}
/// Fields verified using raw direct responses, including absent songname on UGC
/// cards and nested hash on the AI card. Unmapped provider facts remain intact.
pub fn convert(id: u32, bytes: &[u8]) -> Result<Card, ApiError> {
    let mut d = data(bytes)?;
    let info = &d["card_info"];
    let title = info["card_title"].as_str().unwrap_or_default().to_owned();
    let description = ["song_name", "label_desc"]
        .into_iter()
        .filter_map(|key| info[key].as_str())
        .find(|text| !text.trim().is_empty())
        .map(str::to_owned)
        .unwrap_or_else(|| {
            if id == 3101 {
                ["start_day", "end_day"]
                    .into_iter()
                    .filter_map(|key| info[key].as_str())
                    .filter(|text| !text.trim().is_empty())
                    .collect::<Vec<_>>()
                    .join(" — ")
            } else if id == 3103 {
                d["tag_list"]
                    .as_array()
                    .into_iter()
                    .flatten()
                    .take(3)
                    .filter_map(|tag| tag["label_name"].as_str())
                    .filter(|text| !text.trim().is_empty())
                    .collect::<Vec<_>>()
                    .join(" · ")
            } else {
                String::new()
            }
        });
    let mut rows = d["song_list"].as_array().cloned().unwrap_or_default();
    let mut group_specs = Vec::new();
    if id == 3103 {
        if let Some(tags) = d["tag_list"].as_array() {
            for tag in tags {
                if let Some(list) = tag["song_list"].as_array() {
                    group_specs.push((tag.clone(), rows.len(), list.len()));
                    rows.extend(list.iter().cloned());
                }
            }
        }
    }
    if id == 3107 {
        rows = d["list"].as_array().cloned().unwrap_or_default();
    }
    let songs: Vec<DiscoverySong> = rows
        .into_iter()
        .map(ai_recommend_song::convert_song)
        .collect::<Result<Vec<_>, _>>()?;
    if id == 3102 && info["label_name"].as_str().is_some() {
        group_specs.push((info.clone(), 0, songs.len()));
    }
    let groups = group_specs
        .into_iter()
        .map(|(mut raw, start, count)| {
            let preview = songs.iter().skip(start).take(count.min(3)).map(|song| {
                json!({"id":song.id,"title":song.title,"artist":song.artists.iter().map(|a|a.name.as_str()).collect::<Vec<_>>().join(" / ")})
            }).collect::<Vec<_>>();
            if let Some(o) = raw.as_object_mut() {
                o.remove("song_list");
            }
            json!({
                "id":raw["label_id"].clone(),
                "title":raw["label_name"].as_str().unwrap_or_default(),
                "description":raw["label_desc"].as_str().unwrap_or_default(),
                "cover_url":raw["top_cover_pic"].as_str().filter(|s|!s.is_empty()).or(raw["cover_pic"].as_str()).unwrap_or_default().replace("{size}","240"),
                "color":raw["label_color"].as_str().unwrap_or_default(),
                "start_index":start,"song_count":count,"preview":preview,
                "provider_extension":raw
            })
        }).collect();
    let count = d["song_count"].as_u64();
    let items = Vec::new();
    if let Some(o) = d.as_object_mut() {
        o.remove("song_list");
        if id == 3107 {
            o.remove("list");
        }
        if id == 3103 {
            o.remove("tag_list");
        }
    }
    Ok(Card {
        id,
        title,
        description,
        songs,
        items,
        groups,
        count,
        provider_extension: d,
    })
}
pub async fn single(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
    fixed: Option<u32>,
) -> Result<Vec<u8>, ApiError> {
    let id = fixed.unwrap_or(q.card_id);
    let sub = build(id, &q, identity, request::unix_seconds()?)?;
    let url = reqwest::Url::parse(&sub.url).map_err(|_| ApiError::encoding())?;
    let params = url
        .query_pairs()
        .map(|(k, v)| (k.into_owned(), v.into_owned()))
        .collect();
    let bytes = send(
        client,
        identity,
        url.path(),
        params,
        sub.body.into_bytes(),
        if sub.method == "GET" {
            reqwest::Method::GET
        } else {
            reqwest::Method::POST
        },
    )
    .await?;
    request::encode(&convert(id, &bytes)?)
}
pub async fn recommend(
    client: &ApiClient,
    identity: &Identity,
    q: Query,
) -> Result<Vec<u8>, ApiError> {
    let mut ids = match &q.cards {
        Some(Value::Array(a)) => a
            .iter()
            .filter_map(|v| v.as_u64().and_then(|n| u32::try_from(n).ok()))
            .collect::<Vec<_>>(),
        Some(Value::String(s)) => s.split(',').filter_map(|n| n.trim().parse().ok()).collect(),
        _ => CARDS
            .iter()
            .map(|(n, _)| *n)
            .filter(|n| *n != 3007 || !q.source_mixsong.is_empty())
            .collect(),
    };
    ids.retain(|n| module(*n).is_some());
    let mut unique = BTreeSet::new();
    ids.retain(|n| unique.insert(*n));
    if ids.is_empty() {
        return Err(ApiError::new("input", "推荐卡片不能为空"));
    }
    let size = q.card_size.unwrap_or(3);
    let size = if size == 0 { 3 } else { size.min(3) } as usize;
    let mut remaining = ids.clone();
    let mut seen = BTreeSet::new();
    let mut responses = Vec::new();
    let mut rounds = 0;
    let mut last = Value::Null;
    while !remaining.is_empty() && rounds < ids.len() + 3 {
        let endpoint = if rounds == 0 && q.endpoint != "listen" {
            "youth_recommend"
        } else {
            "listen"
        };
        let batch = if rounds == 0 && endpoint == "youth_recommend" {
            ids.clone()
        } else {
            remaining.iter().take(size).copied().collect()
        };
        let time = request::unix_seconds()?;
        let children = batch
            .iter()
            .map(|id| build(*id, &q, identity, time))
            .collect::<Result<Vec<_>, _>>()?;
        let mut body = json!({"requests":children});
        if endpoint == "youth_recommend" && !q.ufeature.is_empty() {
            body["ufeature"] = json!(q.ufeature);
        }
        let body = serde_json::to_vec(&body).map_err(|_| ApiError::encoding())?;
        let mut params = common(identity, time);
        for k in ["area_code", "platform"] {
            params.remove(k);
        }
        if endpoint == "youth_recommend" {
            params.insert("card_size".into(), size.to_string());
            params.insert(
                "card_rank".into(),
                batch
                    .iter()
                    .map(u32::to_string)
                    .collect::<Vec<_>>()
                    .join(","),
            );
        }
        params.insert("signature".into(), signature::android(&params, &body));
        let bytes = send(
            client,
            identity,
            &format!("/card/v1/pxy/{endpoint}"),
            params,
            body,
            reqwest::Method::POST,
        )
        .await?;
        last = data(&bytes)?;
        rounds += 1;
        let before = remaining.len();
        if let Some(list) = last["responses"].as_array() {
            for item in list {
                let name = item["module"].as_str().unwrap_or_default();
                if seen.insert(name.to_owned()) {
                    let mut child = item.clone();
                    if let Some(s) = item["body"].as_str().filter(|_| q.parse) {
                        if let Ok(parsed) = serde_json::from_str::<Value>(s) {
                            child["body"] = parsed;
                        }
                    }
                    responses.push(child);
                }
                remaining.retain(|id| module(*id) != Some(name));
            }
        }
        if !q.full || remaining.len() == before {
            break;
        }
    }
    // Keep every raw subresponse and business error, including modules that could
    // not produce content. The UI selects direct typed cards as they enter view.
    if !last.is_object() {
        last = json!({});
    }
    last["responses"] = json!(responses);
    last["batch_count"] = json!(rounds);
    last["remaining"] = json!(remaining);
    request::encode(&last)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn identity() -> Identity {
        serde_json::from_value(json!({"mid":"test-mid","dfid":"test-dfid"})).unwrap()
    }
    #[test]
    fn concept_children_sign_fixed_identity_exact_body_and_special_shapes() {
        let id = identity();
        let q = Query::default();
        for (cid, name) in CARDS {
            let sub = build(*cid, &q, &id, 123456).unwrap();
            assert_eq!(sub.module, *name);
            let url = reqwest::Url::parse(&sub.url).unwrap();
            let mut params = url
                .query_pairs()
                .map(|(k, v)| (k.into_owned(), v.into_owned()))
                .collect::<BTreeMap<_, _>>();
            assert_eq!(params["appid"], "3116");
            assert_eq!(params["clientver"], "11621");
            assert_eq!(params["clienttime"], "123456");
            let proof = params.remove("signature").unwrap();
            assert_eq!(proof, signature::android(&params, sub.body.as_bytes()));
            if *cid == 3103 {
                assert!(!params.contains_key("card_id"));
                assert!(!params.contains_key("pagesize"));
            }
            if *cid == 3107 {
                let body: Value = serde_json::from_str(&sub.body).unwrap();
                assert_eq!(body["source"], 1);
                assert_eq!(body["pagesize"], 6);
            }
        }
        for removed in [3104, 3105, 3106, 9999] {
            assert!(module(removed).is_none());
            assert!(build(removed, &q, &id, 1).is_err());
        }
    }
    #[test]
    fn unknown_song_fields_survive() {
        let card=convert(3001,br#"{"status":1,"error_code":0,"data":{"song_list":[{"mixsongid":"41","ori_audio_name":"Title","suffix_audio_name":"Live","extra_fact":{"value":7},"audio_info":{"hash":"H","filesize_320":123}}]}}"#).unwrap();
        assert_eq!(card.songs.len(), 1);
        assert_eq!(card.songs[0].song.title, "Title (Live)");
        assert_eq!(card.songs[0].song.hash, "H");
        assert_eq!(
            card.songs[0].metadata.provider_extension.as_ref().unwrap()["extra_fact"]["value"],
            7
        );
    }
}
