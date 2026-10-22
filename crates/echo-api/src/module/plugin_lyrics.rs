//! Plugin-authored lyric documents, parsed on the host worker into the same
//! domain values as downloaded and embedded lyrics. No upstream DTO conversion.
use super::lyric::{self, LyricLine, LyricWord, Lyrics};
use crate::error::ApiError;
use serde::Deserialize;
use std::{collections::BTreeMap, sync::OnceLock};

#[derive(Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct Document {
    pub format: String,
    pub content: String,
    #[serde(default = "text_encoding")]
    pub encoding: String,
    #[serde(default)]
    pub source: String,
    #[serde(default)]
    pub translation: Option<String>,
    #[serde(default)]
    pub romanization: Option<String>,
}
fn text_encoding() -> String {
    "text".into()
}

pub fn parse(document: Document) -> Result<Lyrics, ApiError> {
    if document.content.len() > 1024 * 1024
        || document.source.len() > 1024
        || document
            .translation
            .as_ref()
            .is_some_and(|s| s.len() > 1024 * 1024)
        || document
            .romanization
            .as_ref()
            .is_some_and(|s| s.len() > 1024 * 1024)
    {
        return Err(ApiError::resource_limit());
    }
    let decoded;
    let text = match (document.format.as_str(), document.encoding.as_str()) {
        ("krc", "base64") => {
            decoded = lyric::decode_krc(&document.content)?;
            decoded.as_str()
        }
        ("lrc" | "krc" | "yrc", "text") => document.content.as_str(),
        _ => {
            return Err(ApiError::new(
                "input",
                "Unsupported lyric format or encoding",
            ));
        }
    }
    .trim_start_matches('\u{feff}');
    let mut result = match document.format.as_str() {
        "krc" => lyric::parse_krc(text)?,
        "yrc" => parse_yrc(text)?,
        _ => {
            let parsed = lyric::parse_lrc_document(text)?;
            if !parsed.lines.is_empty() || text.trim().is_empty() {
                parsed
            } else {
                let metadata =
                    regex::Regex::new(r"(?i)^\[(?:id|ar|ti|al|by|offset|hash|language|length):")
                        .unwrap();
                let plain = text
                    .lines()
                    .filter(|line| !metadata.is_match(line.trim()))
                    .collect::<Vec<_>>()
                    .join("\n");
                let mut value = lyric::parse_text(&plain, false)?;
                value.offset_ms = parsed.offset_ms;
                value
            }
        }
    };
    if document.format != "lrc" && !text.trim().is_empty() && result.lines.is_empty() {
        return Err(ApiError::new(
            "input",
            "Lyric document contains no valid lines",
        ));
    }
    let words: usize = result.lines.iter().map(|l| l.words.len()).sum();
    if result.lines.len() > 20000 || words > 100000 {
        return Err(ApiError::resource_limit());
    }
    // Repeated timestamps must not multiply a bounded companion document into
    // an unbounded set of cloned translations/romanizations.
    let mut companion_bytes = 0usize;
    for (companion, translated) in [(document.translation, true), (document.romanization, false)] {
        if let Some(text) = companion {
            let auxiliary = lyric::parse_text(&text, true)?;
            let mut by_time: BTreeMap<u64, String> = BTreeMap::new();
            for line in auxiliary.lines {
                let value = by_time.entry(line.start_ms).or_default();
                if !value.is_empty() {
                    value.push('\n');
                }
                value.push_str(&line.text);
            }
            for line in &mut result.lines {
                if let Some(text) = by_time.get(&line.start_ms) {
                    companion_bytes = companion_bytes
                        .checked_add(text.len())
                        .filter(|size| *size <= 4 * 1024 * 1024)
                        .ok_or_else(ApiError::resource_limit)?;
                    if translated {
                        line.translation = text.clone();
                    } else {
                        line.romanization = text.clone();
                        line.romanization_segments.clear();
                    }
                }
            }
        }
    }
    const MAX_TIME: u64 = 9007199254740991;
    if result.offset_ms.unsigned_abs() > MAX_TIME
        || result.lines.iter().any(|l| {
            l.start_ms > MAX_TIME
                || l.duration_ms > MAX_TIME.saturating_sub(l.start_ms)
                || l.words.iter().any(|w| {
                    w.start_ms > MAX_TIME || w.duration_ms > MAX_TIME.saturating_sub(w.start_ms)
                })
        })
    {
        return Err(ApiError::numeric_overflow());
    }
    Ok(result)
}

fn parse_yrc(text: &str) -> Result<Lyrics, ApiError> {
    static LINE: OnceLock<regex::Regex> = OnceLock::new();
    static WORD: OnceLock<regex::Regex> = OnceLock::new();
    let line_pattern = LINE.get_or_init(|| regex::Regex::new(r"^\[(\d+),(\d+)\](.*)$").unwrap());
    let word_pattern = WORD.get_or_init(|| regex::Regex::new(r"\((\d+),(\d+),\d+\)").unwrap());
    let mut result = Lyrics {
        scrollable: true,
        offset_ms: 0,
        lines: vec![],
    };
    for source in text.lines() {
        let source = source.trim_end_matches('\r');
        if let Some(offset) = source
            .strip_prefix("[offset:")
            .and_then(|s| s.strip_suffix(']'))
        {
            result.offset_ms = offset.parse().map_err(|_| ApiError::decode())?;
            continue;
        }
        let Some(line) = line_pattern.captures(source) else {
            continue;
        };
        let start: u64 = line[1].parse().map_err(|_| ApiError::decode())?;
        let duration: u64 = line[2].parse().map_err(|_| ApiError::decode())?;
        start
            .checked_add(duration)
            .ok_or_else(ApiError::numeric_overflow)?;
        let body = &line[3];
        let tags: Vec<_> = word_pattern.captures_iter(body).collect();
        let mut words = vec![];
        if let Some(first) = tags
            .first()
            .map(|t| t.get(0).unwrap().start())
            .filter(|p| *p > 0)
        {
            words.push(LyricWord {
                start_ms: start,
                duration_ms: 0,
                text: body[..first].into(),
            });
        }
        for (index, tag) in tags.iter().enumerate() {
            // YRC word positions are absolute, unlike KRC's line-relative tags.
            let at: u64 = tag[1].parse().map_err(|_| ApiError::decode())?;
            let length: u64 = tag[2].parse().map_err(|_| ApiError::decode())?;
            at.checked_add(length)
                .ok_or_else(ApiError::numeric_overflow)?;
            let begin = tag.get(0).unwrap().end();
            let end = tags
                .get(index + 1)
                .map_or(body.len(), |t| t.get(0).unwrap().start());
            words.push(LyricWord {
                start_ms: at,
                duration_ms: length,
                text: body[begin..end].into(),
            });
        }
        let text = if words.is_empty() {
            body.into()
        } else {
            words.iter().map(|w| w.text.as_str()).collect()
        };
        result.lines.push(LyricLine {
            start_ms: start,
            duration_ms: duration,
            text,
            words,
            translation: String::new(),
            romanization: String::new(),
            romanization_segments: vec![],
        });
    }
    result.lines.sort_by_key(|line| line.start_ms);
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn doc(format: &str, content: &str) -> Document {
        Document {
            format: format.into(),
            content: content.into(),
            encoding: "text".into(),
            source: "test".into(),
            translation: None,
            romanization: None,
        }
    }
    #[test]
    fn formats_keep_their_time_units_and_one_offset() {
        let lrc = parse(doc("lrc", "[offset:-125]\n[00:01.250]One\n[00:03]Two")).unwrap();
        assert_eq!(lrc.offset_ms, -125);
        assert_eq!(lrc.lines[0].start_ms, 1250);
        let krc = parse(doc(
            "krc",
            "[offset:150]\n[1000,2000]<0,500,0>你<500,1500,0>好",
        ))
        .unwrap();
        assert_eq!(krc.lines[0].words[1].start_ms, 1500);
        assert_eq!(krc.lines[0].text, "你好");
        let mut value = doc(
            "yrc",
            "[offset:150]\n[1000,2000](1000,500,0)你 (1500,1500,0)好（字）",
        );
        value.translation = Some("[00:01]Hello".into());
        value.romanization = Some("[00:01]ni hao".into());
        let yrc = parse(value).unwrap();
        assert_eq!(yrc.lines[0].words[0].start_ms, 1000);
        assert_eq!(yrc.lines[0].words[1].start_ms, 1500);
        assert_eq!(yrc.lines[0].text, "你 好（字）");
        assert_eq!(yrc.lines[0].translation, "Hello");
        assert_eq!(yrc.lines[0].romanization, "ni hao");
        assert_eq!(yrc.offset_ms, 150);
    }
    #[test]
    fn repeated_timestamps_do_not_multiply_companion_memory() {
        let mut document = doc("yrc", &"[0,100](0,100,0)X\n".repeat(5));
        document.translation = Some(format!("[00:00]{}", "X".repeat(900000)));
        assert!(parse(document).is_err());
    }
    #[test]
    fn compressed_krc_and_plain_empty_documents_are_real_values() {
        use base64::Engine;
        use std::io::Write;
        let text = "[1000,500]<0,500,0>Compressed";
        let mut encoder =
            flate2::write::ZlibEncoder::new(Vec::new(), flate2::Compression::default());
        encoder.write_all(text.as_bytes()).unwrap();
        let key = [
            0x40, 0x47, 0x61, 0x77, 0x5e, 0x32, 0x74, 0x47, 0x51, 0x36, 0x31, 0x2d, 0xce, 0xd2,
            0x6e, 0x69,
        ];
        let mut bytes = b"krc1".to_vec();
        bytes.extend(
            encoder
                .finish()
                .unwrap()
                .iter()
                .enumerate()
                .map(|(i, b)| b ^ key[i % 16]),
        );
        let mut document = doc(
            "krc",
            &base64::engine::general_purpose::STANDARD.encode(bytes),
        );
        document.encoding = "base64".into();
        assert_eq!(parse(document).unwrap().lines[0].text, "Compressed");
        let plain = parse(doc("lrc", "[ar:Artist]\nPlain text")).unwrap();
        assert!(!plain.scrollable);
        assert_eq!(plain.lines.len(), 1);
        assert!(parse(doc("lrc", "")).unwrap().lines.is_empty());
        assert!(parse(doc("yrc", "garbage")).is_err());
        assert!(parse(doc("krc", "garbage")).is_err());
        assert!(
            parse(doc(
                "yrc",
                "[18446744073709551615,1](18446744073709551615,1,0)X"
            ))
            .is_err()
        );
    }
}
