//! KRC download with bounded native decoding.
use crate::{
    error::ApiError,
    util::request::{self, ApiClient, Endpoint, Identity, Origin},
};
use base64::{Engine, engine::general_purpose::STANDARD};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, io::Read, sync::OnceLock};
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub id: String,
    pub accesskey: String,
}
#[derive(Deserialize)]
struct Wire {
    #[serde(deserialize_with = "crate::util::response::scalar")]
    status: u32,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    error_code: i64,
    #[serde(deserialize_with = "crate::util::response::scalar")]
    contenttype: u32,
    #[serde(default, deserialize_with = "crate::util::response::text")]
    content: String,
}
#[derive(Serialize)]
pub struct LyricWord {
    pub start_ms: u64,
    pub duration_ms: u64,
    pub text: String,
}
#[derive(Serialize)]
pub struct LyricLine {
    pub start_ms: u64,
    pub duration_ms: u64,
    pub text: String,
    pub words: Vec<LyricWord>,
    pub translation: String,
    pub romanization: String,
    pub romanization_segments: Vec<String>,
}
// Translation and romanization arrays use original KRC line order,
// before chronological sorting.
#[derive(Deserialize)]
struct WireLanguage {
    #[serde(default, deserialize_with = "crate::util::response::list")]
    content: Vec<WireLanguageContent>,
}
#[derive(Deserialize)]
struct WireLanguageContent {
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    language: Option<u32>,
    #[serde(rename = "type")]
    #[serde(default, deserialize_with = "crate::util::response::optional")]
    kind: Option<u32>,
    #[serde(rename = "lyricContent")]
    #[serde(default, deserialize_with = "crate::util::response::list")]
    lines: Vec<Vec<String>>,
}
#[derive(Serialize)]
pub struct Lyrics {
    pub scrollable: bool,
    pub offset_ms: i64,
    pub lines: Vec<LyricLine>,
}
pub fn decode_krc(encoded: &str) -> Result<String, ApiError> {
    if encoded.len() > 4 * 1024 * 1024 {
        return Err(ApiError::resource_limit());
    }
    let bytes = STANDARD.decode(encoded).map_err(|_| ApiError::decode())?;
    if !bytes.starts_with(b"krc1") {
        return Err(ApiError::decode());
    }
    const KEY: [u8; 16] = [
        0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2, 0x6e,
        0x69,
    ];
    let packed: Vec<u8> = bytes[4..]
        .iter()
        .enumerate()
        .map(|(i, b)| b ^ KEY[i % 16])
        .collect();
    let mut text = String::new();
    flate2::read::ZlibDecoder::new(packed.as_slice())
        .take(4 * 1024 * 1024 + 1)
        .read_to_string(&mut text)
        .map_err(|_| ApiError::decode())?;
    if text.len() > 4 * 1024 * 1024 {
        return Err(ApiError::resource_limit());
    }
    Ok(text)
}
pub fn parse_krc(text: &str) -> Result<Lyrics, ApiError> {
    static LINE: OnceLock<regex::Regex> = OnceLock::new();
    static WORD: OnceLock<regex::Regex> = OnceLock::new();
    let line = LINE.get_or_init(|| regex::Regex::new(r"^\[(\d+),(\d+)\](.*)$").unwrap());
    let word = WORD.get_or_init(|| regex::Regex::new(r"<(\d+),(\d+),\d+>([^<]*)").unwrap());
    let mut result = Lyrics {
        scrollable: true,
        offset_ms: 0,
        lines: vec![],
    };
    let mut language = None;
    if text.len() > 4 * 1024 * 1024 {
        return Err(ApiError::resource_limit());
    }
    for source in text.lines() {
        let source = source.trim_end_matches('\r');
        if let Some(encoded) = source
            .strip_prefix("[language:")
            .and_then(|s| s.strip_suffix(']'))
        {
            if encoded.len() > 2 * 1024 * 1024 {
                return Err(ApiError::resource_limit());
            }
            let bytes = STANDARD.decode(encoded).map_err(|_| ApiError::decode())?;
            let wire: WireLanguage = request::decode(&bytes)?;
            language = Some(wire);
            continue;
        }
        if let Some(offset) = source
            .strip_prefix("[offset:")
            .and_then(|s| s.strip_suffix(']'))
        {
            result.offset_ms = offset.parse().map_err(|_| ApiError::decode())?;
            continue;
        }
        let Some(c) = line.captures(source) else {
            continue;
        };
        let start: u64 = c[1].parse().map_err(|_| ApiError::decode())?;
        let mut words = vec![];
        let mut content = String::new();
        for w in word.captures_iter(&c[3]) {
            let relative: u64 = w[1].parse().map_err(|_| ApiError::decode())?;
            content.push_str(&w[3]);
            words.push(LyricWord {
                start_ms: start
                    .checked_add(relative)
                    .ok_or_else(ApiError::numeric_overflow)?,
                duration_ms: w[2].parse().map_err(|_| ApiError::decode())?,
                text: w[3].to_owned(),
            });
        }
        if words.is_empty() {
            content = c[3].to_owned();
        }
        result.lines.push(LyricLine {
            start_ms: start,
            duration_ms: c[2].parse().map_err(|_| ApiError::decode())?,
            text: content,
            words,
            translation: String::new(),
            romanization: String::new(),
            romanization_segments: vec![],
        });
    }
    if let Some(language) = language {
        for content in language.content {
            // Translation/romanization are optional enrichment. Ignore kinds
            // this renderer does not use; zip available lines without rejecting
            // the original lyrics when their lengths differ.
            if content.language != Some(0) || !matches!(content.kind, Some(0 | 1)) {
                continue;
            }
            for (line, segments) in result.lines.iter_mut().zip(content.lines) {
                if content.kind == Some(1) {
                    line.translation = segments.concat().trim().to_owned();
                } else {
                    line.romanization = segments.concat().trim().to_owned();
                    line.romanization_segments = segments;
                }
            }
        }
    }
    result.lines.sort_by_key(|l| l.start_ms);
    Ok(result)
}
// contenttype=1/2 retain fmt=krc but contain base64 UTF-8, not encrypted KRC.
pub fn parse_lrc_document(text: &str) -> Result<Lyrics, ApiError> {
    parse_lrc(text, true, true)
}
pub fn parse_text(text: &str, scrollable: bool) -> Result<Lyrics, ApiError> {
    parse_lrc(text, scrollable, false)
}

fn enhanced_tag() -> &'static regex::Regex {
    static TAG: OnceLock<regex::Regex> = OnceLock::new();
    TAG.get_or_init(|| regex::Regex::new(r"<(\d+):(\d+(?:\.\d+)?)>").unwrap())
}
fn compact_body(body: &str) -> String {
    enhanced_tag()
        .replace_all(body, "")
        .chars()
        .filter(|c| !c.is_whitespace())
        .collect()
}
fn paired(previous: (u64, &str), next: (u64, &str)) -> bool {
    previous.0.abs_diff(next.0) <= 250 && previous.1 != next.1
}
fn lrc_stamp() -> &'static regex::Regex {
    static STAMP: OnceLock<regex::Regex> = OnceLock::new();
    STAMP.get_or_init(|| regex::Regex::new(r"^\[(\d+):(\d+(?:\.\d+)?)\]").unwrap())
}
fn stamp_time(minutes: &str, seconds: &str) -> Result<u64, ApiError> {
    let time = (minutes.parse::<f64>().map_err(|_| ApiError::decode())? * 60.
        + seconds.parse::<f64>().map_err(|_| ApiError::decode())?)
        * 1000.;
    if !time.is_finite() || time < 0. || time >= u64::MAX as f64 {
        return Err(ApiError::numeric_overflow());
    }
    Ok(time.round() as u64)
}
/// Recognize a document layout only at the embedded boundary. Online echoes
/// retain their rows; blank timestamped lines, metadata and KRC rows break
/// adjacency in both passes.
fn paired_layout(text: &str) -> Result<bool, ApiError> {
    let (mut entries, mut pairs) = (0usize, 0usize);
    let mut pending: Option<(u64, String)> = None;
    for source in text.lines() {
        let Some(c) = lrc_stamp().captures(source.trim()) else {
            pending = None;
            continue;
        };
        let body = compact_body(&source.trim()[c[0].len()..]);
        if body.is_empty() {
            pending = None;
            continue;
        }
        entries += 1;
        let at = stamp_time(&c[1], &c[2])?;
        if let Some((time, previous)) = pending.take() {
            if paired((time, &previous), (at, &body)) {
                pairs += 1;
                continue;
            }
        }
        pending = Some((at, body));
    }
    // Clearing pending must not erase already-counted standalone rows:
    // every accepted pair consumes exactly two entries.
    let unpaired = entries.saturating_sub(pairs * 2);
    Ok(pairs >= 2 && unpaired <= 2usize.max((entries as f64 * 0.3).round() as usize))
}
fn enhanced_words(body: &str, line_start: u64) -> Result<Vec<LyricWord>, ApiError> {
    let mut tags = enhanced_tag()
        .captures_iter(body)
        .map(|c| {
            Ok((
                c.get(0).unwrap().start(),
                c.get(0).unwrap().end(),
                stamp_time(&c[1], &c[2])?,
            ))
        })
        .collect::<Result<Vec<_>, ApiError>>()?;
    if tags.is_empty() {
        return Ok(vec![]);
    }
    if !body[..tags[0].0].trim().is_empty() {
        tags.insert(0, (0, 0, line_start));
    }
    let mut words = vec![];
    for (i, &(_, end, at)) in tags.iter().enumerate() {
        let content = &body[end..tags.get(i + 1).map_or(body.len(), |t| t.0)];
        if content.is_empty() {
            continue;
        }
        let next = match tags.get(i + 1) {
            Some(t) => t.2,
            None => at
                .checked_add(3000)
                .ok_or_else(ApiError::numeric_overflow)?,
        };
        words.push(LyricWord {
            start_ms: at,
            duration_ms: next.saturating_sub(at).max(1),
            text: content.to_owned(),
        });
    }
    Ok(words)
}
fn parse_lrc(text: &str, scrollable: bool, embedded: bool) -> Result<Lyrics, ApiError> {
    if text.len() > 4 * 1024 * 1024 {
        return Err(ApiError::resource_limit());
    }
    let timestamp = regex::Regex::new(r"\[(\d{1,4}):(\d{1,2})(?:\.(\d{1,3}))?\]").unwrap();
    let mut result = Lyrics {
        scrollable,
        offset_ms: 0,
        lines: vec![],
    };
    let pair_layout = embedded && paired_layout(text)?;
    let mut pending: Option<(usize, u64)> = None;
    for source in text.lines() {
        let source = source.trim();
        if source.is_empty() {
            pending = None;
            continue;
        }
        if let Some(offset) = source
            .strip_prefix("[offset:")
            .and_then(|v| v.strip_suffix(']'))
        {
            result.offset_ms = offset.parse().map_err(|_| ApiError::decode())?;
            pending = None;
            continue;
        }
        if source.starts_with('[') && source.split(']').next().is_some_and(|s| s.contains(',')) {
            pending = None;
        }
        if !scrollable {
            result.lines.push(LyricLine {
                start_ms: 0,
                duration_ms: 0,
                text: source.to_owned(),
                words: vec![],
                translation: String::new(),
                romanization: String::new(),
                romanization_segments: vec![],
            });
        } else {
            let mut end = 0;
            let mut starts = vec![];
            for capture in timestamp.captures_iter(source) {
                let matched = capture.get(0).unwrap();
                if matched.start() != end {
                    break;
                }
                end = matched.end();
                let minute: u64 = capture[1].parse().map_err(|_| ApiError::decode())?;
                let second: u64 = capture[2].parse().map_err(|_| ApiError::decode())?;
                let fraction = capture.get(3).map(|v| v.as_str()).unwrap_or("");
                let milliseconds: u64 = if fraction.is_empty() {
                    0
                } else {
                    fraction.parse::<u64>().map_err(|_| ApiError::decode())?
                        * 10_u64.pow(3 - fraction.len() as u32)
                };
                starts.push((minute * 60 + second) * 1000 + milliseconds);
            }
            let content = source[end..].trim();
            // A line with only inline timestamps (or none at all) is a
            // separator, not lyric text. Detection already treats it as blank;
            // parsing must not create a row that the next line can pair with.
            if compact_body(content).is_empty() {
                pending = None;
                continue;
            }
            if pair_layout && starts.len() == 1 {
                if let Some((index, at)) = pending.take() {
                    if paired(
                        (at, &compact_body(&result.lines[index].text)),
                        (starts[0], &compact_body(content)),
                    ) {
                        result.lines[index].translation =
                            enhanced_tag().replace_all(content, "").trim().to_owned();
                        continue;
                    }
                }
            } else {
                pending = None;
            }
            for start in starts {
                let words = enhanced_words(content, start)?;
                let line_text = if words.is_empty() {
                    content.to_owned()
                } else {
                    words.iter().map(|w| w.text.as_str()).collect()
                };
                let actual_start = words.first().map_or(start, |w| w.start_ms);
                let duration = words.last().map_or(0, |w| {
                    w.start_ms
                        .saturating_sub(actual_start)
                        .saturating_add(w.duration_ms)
                });
                result.lines.push(LyricLine {
                    start_ms: actual_start,
                    duration_ms: duration,
                    text: line_text,
                    words,
                    translation: String::new(),
                    romanization: String::new(),
                    romanization_segments: vec![],
                });
                pending = Some((result.lines.len() - 1, start));
            }
        }
    }
    if scrollable {
        result.lines.sort_by_key(|l| l.start_ms);
        for i in 0..result.lines.len().saturating_sub(1) {
            if result.lines[i].words.is_empty() {
                result.lines[i].duration_ms =
                    result.lines[i + 1].start_ms - result.lines[i].start_ms;
            }
        }
    }
    Ok(result)
}
/// Local reader tags are plain KRC/LRC/text, never the online base64 envelope.
pub fn parse_embedded(text: &str) -> Result<Lyrics, ApiError> {
    let text = text.trim_matches(|c: char| c.is_whitespace() || c == '\0' || c == '\u{feff}');
    let mut krc = parse_krc(text)?;
    let lrc = parse_lrc(text, true, true)?;
    if !krc.lines.is_empty() {
        krc.lines.extend(lrc.lines);
        krc.lines.sort_by_key(|line| line.start_ms);
        return Ok(krc);
    }
    if !lrc.lines.is_empty() {
        return Ok(lrc);
    }
    let header =
        regex::Regex::new(r"(?i)^\[(?:id|ar|ti|al|by|offset|hash|language|kana):").unwrap();
    let plain = text
        .lines()
        .filter(|line| !header.is_match(line.trim()))
        .collect::<Vec<_>>()
        .join("\n");
    parse_text(&plain, false)
}
pub fn convert(bytes: &[u8]) -> Result<Lyrics, ApiError> {
    let wire: Wire = request::decode(bytes)?;
    if wire.status != 200 || wire.error_code != 0 {
        return Err(ApiError::new("business", "歌词获取失败"));
    }
    if wire.contenttype > 2 {
        return Err(ApiError::new("unsupported", "暂不支持此歌词编码"));
    }
    if wire.contenttype == 0 {
        return parse_krc(&decode_krc(&wire.content)?);
    }
    if wire.content.len() > 4 * 1024 * 1024 {
        return Err(ApiError::resource_limit());
    }
    let bytes = STANDARD
        .decode(&wire.content)
        .map_err(|_| ApiError::decode())?;
    let text = std::str::from_utf8(&bytes).map_err(|_| ApiError::decode())?;
    parse_text(text, wire.contenttype == 1)
}
pub async fn execute(
    client: &ApiClient,
    identity: &Identity,
    query: Query,
) -> Result<Vec<u8>, ApiError> {
    if query.id.is_empty() || query.accesskey.is_empty() {
        return Err(ApiError::new("input", "歌词标识不完整"));
    }
    let bytes = client
        .request_endpoint(
            identity,
            Endpoint {
                origin: Origin::Lyrics,
                path: "/download",
                router: None,
                method: reqwest::Method::GET,
                params: BTreeMap::from([
                    ("ver".into(), "1".into()),
                    ("client".into(), "android".into()),
                    ("id".into(), query.id),
                    ("accesskey".into(), query.accesskey),
                    ("fmt".into(), "krc".into()),
                    ("charset".into(), "utf8".into()),
                ]),
                body: vec![],
                headers: &[],
            },
        )
        .await?;
    request::encode(&convert(&bytes)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn embedded_paired_lines_keep_tolerance_echoes_gaps_and_online_rows() {
        let source =
            "[00:01.00]First line\n[00:01.20]第一行\n[00:05.00]Second line\n[00:04.90]第二行";
        let paired = parse_embedded(source).unwrap();
        assert_eq!(paired.lines.len(), 2);
        assert_eq!(paired.lines[0].translation, "第一行");
        assert_eq!(paired.lines[1].translation, "第二行");
        assert!(paired.lines.iter().all(|l| l.words.is_empty()));
        assert_eq!(parse_text(source, true).unwrap().lines.len(), 4);
        let echo = parse_embedded("[00:01]Echo\n[00:01]Echo\n[00:03]Again\n[00:03]Again").unwrap();
        assert_eq!(echo.lines.len(), 4);
        assert!(echo.lines.iter().all(|l| l.translation.is_empty()));
        let sparse =
            parse_embedded("[00:01]One\n[00:01]一\n[00:03]Two\n[00:05]Three\n[00:07]Four").unwrap();
        assert_eq!(sparse.lines.len(), 5);
        let blank = parse_embedded("[00:01]One\n[00:01]一\n[00:03]Two\n[00:03]二\n[00:05]Keep\n[00:05]\n[00:05]New primary").unwrap();
        assert_eq!(blank.lines.len(), 4);
        assert_eq!(blank.lines[2].text, "Keep");
        assert!(blank.lines[2].translation.is_empty());
    }
    #[test]
    fn paired_layout_rejects_separators_and_counts_gaps() {
        let mixed = "[00:10.00]原词一\n[10100,2000]<0,500,0>K\n[00:10.10]译文一\n[00:20.00]原词二\n[00:20.00]译文二\n[00:30.00]原词三\n[00:30.00]译文三";
        let lyrics = parse_embedded(mixed).unwrap();
        assert_eq!(lyrics.lines.len(), 5);
        assert_eq!(lyrics.lines[0].text, "原词一");
        assert!(lyrics.lines[0].translation.is_empty());
        assert_eq!(lyrics.lines[2].text, "译文一");
        assert_eq!(lyrics.lines[3].translation, "译文二");
        assert_eq!(lyrics.lines[4].translation, "译文三");

        for separator in ["", "   ", "[ar:Artist]", "[10100,2000]<0,500,0>Interlude"] {
            let source = format!(
                "[00:10.00]First\n{separator}\n[00:10.10]Second\n[00:20.00]Third\n[00:20.00]Fourth"
            );
            let lyrics = parse_embedded(&source).unwrap();
            assert!(
                lyrics.lines.iter().all(|line| line.translation.is_empty()),
                "{separator:?}"
            );
            assert_eq!(lyrics.lines.last().unwrap().text, "Fourth");
        }
    }
    #[test]
    fn timed_empty_lines_do_not_hide_unpaired_rows() {
        let mut source = String::from(
            "[00:01.00]First voice\n[00:01.10]Second voice\n[00:02.00]Third voice\n[00:02.10]Fourth voice",
        );
        for i in 0..8 {
            source.push_str(&format!(
                "\n[00:{:02}.00]Solo {i}\n[00:{:02}.50]",
                10 + i,
                10 + i
            ));
        }
        let lyrics = parse_embedded(&source).unwrap();
        assert_eq!(lyrics.lines.len(), 12);
        assert!(lyrics.lines.iter().all(|line| line.translation.is_empty()));
    }
    #[test]
    fn blank_separator_survives_other_pairs() {
        let source = "[00:10.00]First voice\n\n[00:10.10]Second voice\n[ti:Song]\n[00:53.02]Baby just let me let go\n[00:53.02]允许我离开\n[00:57.10]You were the shadow\n[00:57.10]你是我的影子";
        let lyrics = parse_embedded(source).unwrap();
        let texts = lyrics
            .lines
            .iter()
            .map(|line| line.text.as_str())
            .collect::<Vec<_>>();
        assert_eq!(
            texts,
            [
                "First voice",
                "Second voice",
                "Baby just let me let go",
                "You were the shadow"
            ]
        );
        assert!(lyrics.lines[0].translation.is_empty());
        assert!(lyrics.lines[1].translation.is_empty());
        assert_eq!(lyrics.lines[2].translation, "允许我离开");
        assert_eq!(lyrics.lines[3].translation, "你是我的影子");
    }
    #[test]
    fn timestamp_only_enhanced_line_is_a_separator() {
        let lyrics = parse_embedded(
            "[00:10.00]原词一\n[00:10.00]<00:10.00>\n[00:10.10]原词二\n[00:20.00]原词三\n[00:20.00]译文三\n[00:30.00]原词四\n[00:30.00]译文四",
        )
        .unwrap();
        let texts = lyrics
            .lines
            .iter()
            .map(|line| line.text.as_str())
            .collect::<Vec<_>>();
        assert_eq!(texts, ["原词一", "原词二", "原词三", "原词四"]);
        assert!(lyrics.lines[0].translation.is_empty());
        assert!(lyrics.lines[1].translation.is_empty());
        assert_eq!(lyrics.lines[2].translation, "译文三");
        assert_eq!(lyrics.lines[3].translation, "译文四");
    }
    #[test]
    fn enhanced_lrc_pairs_strip_tags_but_keep_actual_word_times() {
        let source = "[00:01.00]<00:01.00>A<00:01.50>B<00:02.00>\n[00:01.10]<00:01.10>甲乙\n[00:03.00]<00:03.00>C<00:03.50>D<00:04.00>\n[00:03.00]丙丁";
        let lyrics = parse_embedded(source).unwrap();
        assert_eq!(lyrics.lines.len(), 2);
        assert_eq!(lyrics.lines[0].text, "AB");
        assert_eq!(lyrics.lines[0].translation, "甲乙");
        assert_eq!(lyrics.lines[0].words.len(), 2);
        assert_eq!(lyrics.lines[0].words[1].start_ms, 1500);
        assert_eq!(lyrics.lines[0].words[1].duration_ms, 500);
        let one = parse_embedded("[00:01]prefix<00:02>one<00:03>").unwrap();
        assert_eq!(one.lines[0].words[0].text, "prefix");
        assert_eq!(one.lines[0].words[0].start_ms, 1000);
        let krc = parse_embedded("[1000,2000]无逐字标签").unwrap();
        assert!(krc.lines[0].words.is_empty());
    }
    #[test]
    fn embedded_formats_keep_timed_and_plain_text_distinct() {
        let timed = parse_embedded(
            "\u{feff}[ti:内嵌]\n[offset:120]\n[00:01.00][00:03.50]第一行\n[00:05.00]第二行",
        )
        .unwrap();
        assert!(timed.scrollable);
        assert_eq!(timed.offset_ms, 120);
        assert_eq!(timed.lines.len(), 3);
        assert_eq!(timed.lines[1].start_ms, 3500);
        assert_eq!(timed.lines[0].text, "第一行");
        let plain = parse_embedded("[ar:Artist]\n第一行\nSecond line").unwrap();
        assert!(!plain.scrollable);
        assert_eq!(plain.lines.len(), 2);
        assert_eq!(plain.lines[0].text, "第一行");
        let krc = parse_embedded("[1000,500]<0,250,0>内嵌<250,250,0>歌词").unwrap();
        assert!(krc.scrollable);
        assert_eq!(krc.lines[0].words.len(), 2);
    }
    #[test]
    #[cfg(feature = "local-api-fixtures")]
    fn genuine_manual_download_formats_are_decoded_without_krc_assumptions() {
        for (kind, bytes) in [
            (0, include_bytes!("../../../../tests/fixtures/api/lyric/candidate-kind-0-source-variants2-20261015.json").as_slice()),
            (1, include_bytes!("../../../../tests/fixtures/api/lyric/candidate-kind-1-source-variants2-20261015.json").as_slice()),
            (2, include_bytes!("../../../../tests/fixtures/api/lyric/candidate-kind-2-source-variants2-20261015.json").as_slice()),
        ] {
            let capture: serde_json::Value = serde_json::from_slice(bytes).unwrap();
            let lyrics = convert(&serde_json::to_vec(&capture["responses"][0]["payload"]).unwrap()).unwrap();
            assert!(lyrics.lines.len() > 20);
            assert_eq!(lyrics.scrollable, kind != 2);
            if kind == 2 { assert!(lyrics.lines.iter().all(|l| l.start_ms == 0 && l.words.is_empty())); }
            if kind == 1 { assert!(lyrics.lines.iter().any(|l| l.start_ms > 1000)); assert!(lyrics.lines.iter().all(|l| l.words.is_empty())); }
        }
    }
    #[test]
    #[cfg(feature = "local-api-fixtures")]
    fn genuine_language_arrays_stay_aligned_with_the_original_line_order() {
        for (sample, romanization) in [
            (
                include_str!(
                    "../../../../tests/fixtures/api/lyric/lyric-track-translation-20261013.json"
                ),
                false,
            ),
            (
                include_str!(
                    "../../../../tests/fixtures/api/lyric/lyric-track-romanization-20261013.json"
                ),
                true,
            ),
        ] {
            let sample: serde_json::Value = serde_json::from_str(sample).unwrap();
            let lyrics =
                convert(&serde_json::to_vec(&sample["responses"][0]["payload"]).unwrap()).unwrap();
            assert!(lyrics.lines.iter().any(|l| !l.translation.is_empty()));
            assert_eq!(
                lyrics.lines.iter().any(|l| !l.romanization.is_empty()),
                romanization
            );
            assert!(
                lyrics
                    .lines
                    .windows(2)
                    .all(|l| l[0].start_ms <= l[1].start_ms)
            );
            if !romanization {
                let line = lyrics
                    .lines
                    .iter()
                    .find(|l| l.text.trim() == "When I was young")
                    .unwrap();
                assert_eq!(line.translation, "年轻时候");
            }
        }
        // Corrupt a genuine metadata array rather than fabricate a success DTO.
        let sample: serde_json::Value = serde_json::from_str(include_str!(
            "../../../../tests/fixtures/api/lyric/lyric-track-translation-20261013.json"
        ))
        .unwrap();
        let text = decode_krc(
            sample["responses"][0]["payload"]["content"]
                .as_str()
                .unwrap(),
        )
        .unwrap();
        let metadata = text.lines().find(|l| l.starts_with("[language:")).unwrap();
        let mut language: serde_json::Value =
            serde_json::from_slice(&STANDARD.decode(&metadata[10..metadata.len() - 1]).unwrap())
                .unwrap();
        language["content"][0]["lyricContent"]
            .as_array_mut()
            .unwrap()
            .pop();
        let invalid = text.replace(
            metadata,
            &format!(
                "[language:{}]",
                STANDARD.encode(serde_json::to_vec(&language).unwrap())
            ),
        );
        assert_eq!(
            parse_krc(&invalid).unwrap().lines.len(),
            parse_krc(&text).unwrap().lines.len()
        );
    }
    #[test]
    #[cfg(feature = "local-api-fixtures")]
    fn genuine_krc_decompresses_with_word_timeline() {
        let sample: serde_json::Value = serde_json::from_str(include_str!(
            "../../../../tests/fixtures/api/lyric/playback-song-playback-20261013.json"
        ))
        .unwrap();
        let lyrics =
            convert(&serde_json::to_vec(&sample["responses"][0]["payload"]).unwrap()).unwrap();
        assert!(lyrics.lines.len() > 20);
        assert!(
            lyrics
                .lines
                .windows(2)
                .all(|l| l[0].start_ms <= l[1].start_ms)
        );
        assert!(lyrics.lines.iter().any(|l| l.words.len() > 5));
        for line in &lyrics.lines {
            assert_eq!(
                line.text,
                line.words
                    .iter()
                    .map(|w| w.text.as_str())
                    .collect::<String>()
            );
        }
    }
    #[test]
    fn malformed_compression_offsets_and_timing_overflow_are_rejected() {
        for encoded in ["not base64", "a3JjMQ==", "AAAA"] {
            assert!(decode_krc(encoded).is_err());
        }
        assert!(parse_krc("[offset:bad]").is_err());
        assert!(parse_krc("[18446744073709551615,1]<1,1,0>word").is_err());
        let lyrics = parse_krc("[offset:-250]\n[1000,200]<0,100,0>A<100,100,0>B").unwrap();
        assert_eq!(lyrics.offset_ms, -250);
        assert_eq!(lyrics.lines[0].words[1].start_ms, 1100);
    }
}
