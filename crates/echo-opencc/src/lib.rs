pub mod work;
use once_cell::sync::Lazy;
use opencc_fmmseg::{CustomDictMode, CustomDictSpec, DictSlot, DictionaryMaxlength, OpenCC};
use work::{Command, Work};
pub use work::{EngineError, Result};

fn dictionary_pairs(data: &str) -> Vec<(String, String)> {
    data.lines()
        .filter(|line| !line.trim().is_empty() && !line.starts_with('#'))
        .map(|line| {
            let (source, targets) = line.split_once('\t').expect("invalid bundled OpenCC entry");
            let target = targets
                .split_whitespace()
                .next()
                .expect("empty OpenCC target");
            assert_eq!(source.chars().count(), target.chars().count());
            (source.to_owned(), target.to_owned())
        })
        .collect()
}

static CONVERTER: Lazy<OpenCC> = Lazy::new(|| {
    // Replace only the reverse-variant slots, unused by the existing four profiles.
    // Keep upstream phrase exceptions rather than doing blanket char replacements.
    let dictionary = DictionaryMaxlength::from_zstd()
        .and_then(|dictionary| {
            dictionary.with_custom_dicts(&[
                CustomDictSpec {
                    slot: DictSlot::TWVariantsRev,
                    pairs: dictionary_pairs(include_str!("../dicts/TWVariantsRev.txt")),
                    mode: CustomDictMode::Override,
                },
                CustomDictSpec {
                    slot: DictSlot::TWVariantsRevPhrases,
                    pairs: dictionary_pairs(include_str!("../dicts/TWVariantsRevPhrases.txt")),
                    mode: CustomDictMode::Override,
                },
            ])
        })
        .expect("invalid bundled OpenCC dictionaries");
    let mut converter = OpenCC::from_dictionary(dictionary);
    // Lyric lines are short; the native host worker keeps conversion off the
    // Qt GUI thread without creating another Rayon pool for every process.
    converter.set_parallel(false);
    converter
});

fn normalize_profile(profile: &str) -> crate::Result<&'static str> {
    match profile {
        "s2t" => Ok("s2t"),
        "s2t-variants" => Ok("s2t-variants"),
        "t2s" => Ok("t2s"),
        "s2tw" => Ok("s2tw"),
        "s2hk" => Ok("s2hk"),
        other => Err(crate::EngineError::from_reason(format!(
            "unsupported OpenCC profile: {other}"
        ))),
    }
}

fn convert_text(text: &str, profile: &str) -> String {
    if profile == "s2t-variants" {
        CONVERTER.tw2t(&CONVERTER.s2t(text, false), false)
    } else {
        CONVERTER.convert(text, profile, false)
    }
}

pub fn convert(text: String, profile: String) -> crate::Result<String> {
    let profile = normalize_profile(&profile)?;
    Ok(convert_text(&text, profile))
}

pub struct ConvertBatchTask {
    texts: Vec<String>,
    profile: &'static str,
}

impl Command for ConvertBatchTask {
    type Output = Vec<String>;

    fn compute(&mut self) -> crate::Result<Self::Output> {
        Ok(self
            .texts
            .iter()
            .map(|text| convert_text(text, self.profile))
            .collect())
    }
}

pub fn convert_batch(texts: Vec<String>, profile: String) -> crate::Result<Work<ConvertBatchTask>> {
    Ok(Work::new(ConvertBatchTask {
        texts,
        profile: normalize_profile(&profile)?,
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn variants_keep_phrase_exceptions_and_unicode_segments() {
        let input = vec![
            "因为你这里的群众，峰回路转。".to_owned(),
            "為 群 峰 裡".to_owned(),
            "才华 著作 梁先生 桥梁 人参 参加 口吃 吃饭".to_owned(),
            "𠮷🙂中文かな한글".to_owned(),
        ];
        let expected = vec![
            "因爲你這裏的羣衆，峯迴路轉。".to_owned(),
            "爲 羣 峯 裏".to_owned(),
            "才華 著作 梁先生 橋樑 人蔘 參加 口吃 喫飯".to_owned(),
            "𠮷🙂中文かな한글".to_owned(),
        ];
        assert_eq!(
            convert_batch(input.clone(), "s2t-variants".into())
                .unwrap()
                .execute()
                .unwrap(),
            expected
        );
        for (text, result) in input.iter().zip(&expected) {
            assert_eq!(
                convert(text.clone(), "s2t-variants".into()).unwrap(),
                *result
            );
            assert_eq!(text.chars().count(), result.chars().count());
        }
        assert!(
            convert_batch(vec![], "s2t-variants".into())
                .unwrap()
                .execute()
                .unwrap()
                .is_empty()
        );
    }

    #[test]
    fn existing_profiles_keep_their_dictionary_behavior() {
        for (input, profile, expected) in [
            (
                "因为你这里的群众，峰回路转。",
                "s2t",
                "因為你這裡的群眾，峰迴路轉。",
            ),
            (
                "頭髮乾燥，後來發現理髮店",
                "t2s",
                "头发干燥，后来发现理发店",
            ),
            ("床前明月光", "s2tw", "床前明月光"),
            ("台灯", "s2hk", "枱燈"),
            (
                "才华 著作 梁先生 桥梁 人参 参加 口吃 吃饭",
                "s2t",
                "才華 著作 梁先生 橋樑 人參 參加 口吃 吃飯",
            ),
        ] {
            assert_eq!(convert(input.into(), profile.into()).unwrap(), expected);
            assert_eq!(
                convert_batch(vec![input.into()], profile.into())
                    .unwrap()
                    .execute()
                    .unwrap(),
                vec![expected]
            );
        }
        assert!(convert("歌词".into(), "bad".into()).is_err());
    }
}
