//! Type-directed response decoding. Numeric strings are decoded exactly, never
//! through f64; text identifiers keep their spelling. Optional facts may be
//! unknown without rejecting their enclosing row. This is not used for inputs.
use serde::{
    Deserialize, Deserializer,
    de::{self, DeserializeOwned, IntoDeserializer, Visitor},
};
use serde_json::Value;

type Error = serde_json::Error;

/// Shared wire shapes retain required/default data and error-key distinctions.
/// Endpoint-specific proofs and optional-error envelopes remain local types.
#[derive(Deserialize)]
pub struct Envelope<T> {
    #[serde(deserialize_with = "scalar")]
    pub status: u32,
    #[serde(deserialize_with = "scalar")]
    pub error_code: i64,
    pub data: T,
}

#[derive(Deserialize)]
pub struct DefaultDataEnvelope<T: Default> {
    #[serde(deserialize_with = "scalar")]
    pub status: u32,
    #[serde(deserialize_with = "scalar")]
    pub error_code: i64,
    #[serde(default)]
    pub data: T,
}

#[derive(Deserialize)]
pub struct ErrcodeEnvelope<T> {
    #[serde(deserialize_with = "scalar")]
    pub status: u32,
    #[serde(deserialize_with = "scalar")]
    pub errcode: i64,
    pub data: T,
}

#[derive(Deserialize)]
pub struct AckEnvelope {
    #[serde(deserialize_with = "scalar")]
    pub status: u32,
    #[serde(deserialize_with = "scalar")]
    pub error_code: i64,
}
pub fn scalar<'de, D: Deserializer<'de>, T: DeserializeOwned>(d: D) -> Result<T, D::Error> {
    from_value(Value::deserialize(d)?).map_err(de::Error::custom)
}
/// Display-only text uses the empty display for missing/null/unknown values.
/// Editable preferences and authentication proofs do not use this projection.
pub fn text<'de, D: Deserializer<'de>>(d: D) -> Result<String, D::Error> {
    Ok(from_value(Value::deserialize(d)?).unwrap_or_default())
}
pub fn optional<'de, D: Deserializer<'de>, T: DeserializeOwned>(
    d: D,
) -> Result<Option<T>, D::Error> {
    let value = Value::deserialize(d)?;
    if value.is_null() {
        return Ok(None);
    }
    Ok(from_value(value).ok())
}
/// Business error/proof fields may be absent, but an unreadable present value
/// must not be turned into a successful acknowledgement.
pub fn strict_optional<'de, D: Deserializer<'de>, T: DeserializeOwned>(
    d: D,
) -> Result<Option<T>, D::Error> {
    let value = Value::deserialize(d)?;
    if value.is_null() {
        Ok(None)
    } else {
        from_value(value).map(Some).map_err(de::Error::custom)
    }
}
/// Independent optional values inside a response metadata dictionary.
pub fn number_map<'de, D: Deserializer<'de>, T: DeserializeOwned>(
    d: D,
) -> Result<std::collections::BTreeMap<String, Option<T>>, D::Error> {
    optional_map(d).map(Option::unwrap_or_default)
}
pub fn list<'de, D: Deserializer<'de>, T: DeserializeOwned>(d: D) -> Result<Vec<T>, D::Error> {
    let value = Value::deserialize(d)?;
    if value.is_null() {
        return Ok(Vec::new());
    }
    from_value(value).map_err(de::Error::custom)
}
/// One unknown entry must not discard the known entries of a metadata map.
pub fn optional_map<'de, D: Deserializer<'de>, T: DeserializeOwned>(
    d: D,
) -> Result<Option<std::collections::BTreeMap<String, Option<T>>>, D::Error> {
    let value = Value::deserialize(d)?;
    Ok(value.as_object().map(|values| {
        values
            .iter()
            .map(|(key, value)| (key.clone(), from_value(value.clone()).ok()))
            .collect()
    }))
}
pub fn object<'de, D: Deserializer<'de>, T: DeserializeOwned + Default>(
    d: D,
) -> Result<T, D::Error> {
    Ok(from_value(Value::deserialize(d)?).unwrap_or_default())
}
pub fn from_value<T: DeserializeOwned>(value: Value) -> Result<T, Error> {
    T::deserialize(ResponseValue(value))
}
struct ResponseValue(Value);
impl<'de> IntoDeserializer<'de, Error> for ResponseValue {
    type Deserializer = Self;
    fn into_deserializer(self) -> Self {
        self
    }
}
fn unsigned(value: &Value) -> Option<u64> {
    match value {
        Value::Number(n) => super::numeric::exact_unsigned(n),
        Value::String(s) => s.trim().parse::<u64>().ok().or_else(|| {
            serde_json::from_str::<serde_json::Number>(s.trim())
                .ok()
                .as_ref()
                .and_then(super::numeric::exact_unsigned)
        }),
        _ => None,
    }
}
fn signed(value: &Value) -> Option<i64> {
    if let Some(n) = unsigned(value) {
        return i64::try_from(n).ok();
    }
    let text = match value {
        Value::Number(n) => n.to_string(),
        Value::String(s) => s.trim().to_owned(),
        _ => return None,
    };
    let magnitude = text.strip_prefix('-')?;
    let magnitude = unsigned(&Value::String(magnitude.into()))?;
    if magnitude == 1_u64 << 63 {
        Some(i64::MIN)
    } else {
        i64::try_from(magnitude).ok()?.checked_neg()
    }
}
fn invalid(value: &Value) -> Error {
    let kind = match value {
        Value::Null => "null",
        Value::Bool(_) => "boolean",
        Value::Number(_) => "number",
        Value::String(_) => "string",
        Value::Array(_) => "array",
        Value::Object(_) => "object",
    };
    de::Error::custom(format!("cannot decode {kind} as requested scalar"))
}
macro_rules! integer {
    ($method:ident, $visit:ident, $ty:ty, $parse:ident) => {
        fn $method<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
            let n = $parse(&self.0)
                .and_then(|n| <$ty>::try_from(n).ok())
                .ok_or_else(|| invalid(&self.0))?;
            visitor.$visit(n)
        }
    };
}
impl<'de> Deserializer<'de> for ResponseValue {
    type Error = Error;
    integer!(deserialize_u8, visit_u8, u8, unsigned);
    integer!(deserialize_u16, visit_u16, u16, unsigned);
    integer!(deserialize_u32, visit_u32, u32, unsigned);
    integer!(deserialize_u64, visit_u64, u64, unsigned);
    integer!(deserialize_i8, visit_i8, i8, signed);
    integer!(deserialize_i16, visit_i16, i16, signed);
    integer!(deserialize_i32, visit_i32, i32, signed);
    integer!(deserialize_i64, visit_i64, i64, signed);
    fn deserialize_f64<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        let n = match &self.0 {
            Value::Number(n) => n.as_f64(),
            Value::String(s) => s.trim().parse().ok(),
            _ => None,
        }
        .filter(|n: &f64| n.is_finite())
        .ok_or_else(|| invalid(&self.0))?;
        visitor.visit_f64(n)
    }
    fn deserialize_f32<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        self.deserialize_f64(visitor)
    }
    fn deserialize_bool<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        match &self.0 {
            Value::Bool(b) => visitor.visit_bool(*b),
            Value::String(s) if s == "true" || s == "false" => visitor.visit_bool(s == "true"),
            _ => match unsigned(&self.0) {
                Some(0) => visitor.visit_bool(false),
                Some(1) => visitor.visit_bool(true),
                _ => Err(invalid(&self.0)),
            },
        }
    }
    fn deserialize_string<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        match self.0 {
            Value::String(s) => visitor.visit_string(s),
            Value::Number(n) => visitor.visit_string(
                super::numeric::exact_unsigned(&n)
                    .map(|v| v.to_string())
                    .unwrap_or_else(|| n.to_string()),
            ),
            v => Err(invalid(&v)),
        }
    }
    fn deserialize_str<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        self.deserialize_string(visitor)
    }
    fn deserialize_seq<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        match self.0 {
            Value::Array(values) => visitor.visit_seq(de::value::SeqDeserializer::new(
                values.into_iter().map(ResponseValue),
            )),
            v => Err(invalid(&v)),
        }
    }
    fn deserialize_tuple<V: Visitor<'de>>(self, _: usize, visitor: V) -> Result<V::Value, Error> {
        self.deserialize_seq(visitor)
    }
    fn deserialize_tuple_struct<V: Visitor<'de>>(
        self,
        _: &'static str,
        len: usize,
        visitor: V,
    ) -> Result<V::Value, Error> {
        self.deserialize_tuple(len, visitor)
    }
    fn deserialize_map<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        match self.0 {
            Value::Object(values) => visitor.visit_map(de::value::MapDeserializer::new(
                values.into_iter().map(|(k, v)| (k, ResponseValue(v))),
            )),
            v => Err(invalid(&v)),
        }
    }
    fn deserialize_struct<V: Visitor<'de>>(
        self,
        _: &'static str,
        _: &'static [&'static str],
        visitor: V,
    ) -> Result<V::Value, Error> {
        self.deserialize_map(visitor)
    }
    fn deserialize_option<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        if self.0.is_null() {
            visitor.visit_none()
        } else {
            visitor.visit_some(self)
        }
    }
    fn deserialize_newtype_struct<V: Visitor<'de>>(
        self,
        _: &'static str,
        visitor: V,
    ) -> Result<V::Value, Error> {
        visitor.visit_newtype_struct(self)
    }
    fn deserialize_enum<V: Visitor<'de>>(
        self,
        name: &'static str,
        variants: &'static [&'static str],
        visitor: V,
    ) -> Result<V::Value, Error> {
        self.0.deserialize_enum(name, variants, visitor)
    }
    fn deserialize_any<V: Visitor<'de>>(self, visitor: V) -> Result<V::Value, Error> {
        self.0.deserialize_any(visitor)
    }
    serde::forward_to_deserialize_any! { char bytes byte_buf unit unit_struct identifier ignored_any }
}

/// Remove the named keys a module has consumed from `raw`, optionally walking
/// a dotted `path` first. Absent containers simply yield no change; leftovers
/// stay verbatim for provider_extension capture.
pub fn drop_consumed_keys(raw: &mut Value, path: &str, keys: &[&str]) {
    let mut target = &mut *raw;
    for segment in path.split('.').filter(|part| !part.is_empty()) {
        let Value::Object(map) = target else {
            return;
        };
        let Some(next) = map.get_mut(segment) else {
            return;
        };
        target = next;
    }
    let Value::Object(map) = target else {
        return;
    };
    for key in keys {
        map.remove(*key);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[derive(Deserialize)]
    struct Facts {
        #[serde(default, deserialize_with = "optional")]
        duration: Option<u64>,
        #[serde(default, deserialize_with = "optional")]
        flag: Option<bool>,
        #[serde(default, deserialize_with = "optional_map")]
        map: Option<std::collections::BTreeMap<String, Option<u64>>>,
    }
    #[test]
    fn exact_scalars_and_unknown_neighbors_do_not_round_or_erase_known_facts() {
        for token in ["18446744073709551615", "1.8446744073709551615e19"] {
            let value: u64 = from_value(Value::String(token.into())).unwrap();
            assert_eq!(value, u64::MAX);
        }
        assert_eq!(
            from_value::<u64>(Value::String("  +00123  ".into())).unwrap(),
            123
        );
        assert_eq!(
            from_value::<i64>(Value::String("-9223372036854775808".into())).unwrap(),
            i64::MIN
        );
        assert_eq!(from_value::<String>(serde_json::json!(123)).unwrap(), "123");
        assert_eq!(
            from_value::<String>(serde_json::json!("00123")).unwrap(),
            "00123"
        );
        let facts: Facts = serde_json::from_str(r#"{"duration":"1.25","flag":99,"map":{"known":"2.8162962909184e14","other":{"format":"future"}}}"#).unwrap();
        assert!(facts.duration.is_none());
        assert!(facts.flag.is_none());
        let map = facts.map.unwrap();
        assert_eq!(map["known"], Some(281629629091840));
        assert_eq!(map["other"], None);
        assert!(serde_json::from_str::<Facts>("{broken").is_err());
    }
}
