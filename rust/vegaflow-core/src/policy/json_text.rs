//! Strict JSON (RFC 8259) reader and writer for `config.json` that keeps key order and number text.
//! Skips a leading UTF-8 BOM; refuses comments, trailing commas, repeated keys and nesting past `MAX_DEPTH`.

use std::collections::HashSet;

/// Deepest nesting accepted. `config.json` needs 2; the limit keeps a hostile file from exhausting the stack.
pub const MAX_DEPTH: usize = 64;

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Null,
    Bool(bool),
    /// The number exactly as written in the file (already checked against the JSON grammar).
    Number(String),
    String(String),
    Array(Vec<Value>),
    /// Keys in file order; no key appears twice.
    Object(Vec<(String, Value)>),
}

impl Value {
    /// The value of `key` when `self` is an object.
    pub fn get(&self, key: &str) -> Option<&Value> {
        match self {
            Value::Object(members) => members.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }
}

/// The text is not one well-formed JSON value.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ParseError;

pub fn parse(bytes: &[u8]) -> Result<Value, ParseError> {
    let text = std::str::from_utf8(bytes).map_err(|_| ParseError)?;
    let text = text.strip_prefix('\u{FEFF}').unwrap_or(text);
    let mut p = Parser { s: text.as_bytes(), pos: 0 };
    p.skip_ws();
    let value = p.value(0)?;
    p.skip_ws();
    if p.pos != p.s.len() {
        return Err(ParseError);
    }
    Ok(value)
}

struct Parser<'a> {
    s: &'a [u8],
    pos: usize,
}

impl Parser<'_> {
    fn peek(&self) -> Option<u8> {
        self.s.get(self.pos).copied()
    }

    fn skip_ws(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.pos += 1;
        }
    }

    fn eat(&mut self, b: u8) -> Result<(), ParseError> {
        if self.peek() == Some(b) {
            self.pos += 1;
            Ok(())
        } else {
            Err(ParseError)
        }
    }

    fn literal(&mut self, word: &[u8], value: Value) -> Result<Value, ParseError> {
        if self.s[self.pos..].starts_with(word) {
            self.pos += word.len();
            Ok(value)
        } else {
            Err(ParseError)
        }
    }

    fn value(&mut self, depth: usize) -> Result<Value, ParseError> {
        match self.peek().ok_or(ParseError)? {
            b'{' => self.object(depth + 1),
            b'[' => self.array(depth + 1),
            b'"' => self.string().map(Value::String),
            b't' => self.literal(b"true", Value::Bool(true)),
            b'f' => self.literal(b"false", Value::Bool(false)),
            b'n' => self.literal(b"null", Value::Null),
            b'-' | b'0'..=b'9' => self.number(),
            _ => Err(ParseError),
        }
    }

    fn object(&mut self, depth: usize) -> Result<Value, ParseError> {
        if depth > MAX_DEPTH {
            return Err(ParseError);
        }
        self.eat(b'{')?;
        let mut members: Vec<(String, Value)> = Vec::new();
        let mut seen: HashSet<String> = HashSet::new();
        self.skip_ws();
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Ok(Value::Object(members));
        }
        loop {
            self.skip_ws();
            let key = self.string()?;
            if !seen.insert(key.clone()) {
                return Err(ParseError);
            }
            self.skip_ws();
            self.eat(b':')?;
            self.skip_ws();
            let v = self.value(depth)?;
            members.push((key, v));
            self.skip_ws();
            match self.peek() {
                Some(b',') => self.pos += 1,
                Some(b'}') => {
                    self.pos += 1;
                    return Ok(Value::Object(members));
                }
                _ => return Err(ParseError),
            }
        }
    }

    fn array(&mut self, depth: usize) -> Result<Value, ParseError> {
        if depth > MAX_DEPTH {
            return Err(ParseError);
        }
        self.eat(b'[')?;
        let mut items = Vec::new();
        self.skip_ws();
        if self.peek() == Some(b']') {
            self.pos += 1;
            return Ok(Value::Array(items));
        }
        loop {
            self.skip_ws();
            items.push(self.value(depth)?);
            self.skip_ws();
            match self.peek() {
                Some(b',') => self.pos += 1,
                Some(b']') => {
                    self.pos += 1;
                    return Ok(Value::Array(items));
                }
                _ => return Err(ParseError),
            }
        }
    }

    fn digits(&mut self) -> usize {
        let start = self.pos;
        while matches!(self.peek(), Some(b'0'..=b'9')) {
            self.pos += 1;
        }
        self.pos - start
    }

    fn number(&mut self) -> Result<Value, ParseError> {
        let start = self.pos;
        if self.peek() == Some(b'-') {
            self.pos += 1;
        }
        match self.peek() {
            Some(b'0') => self.pos += 1,
            Some(b'1'..=b'9') => {
                self.digits();
            }
            _ => return Err(ParseError),
        }
        if self.peek() == Some(b'.') {
            self.pos += 1;
            if self.digits() == 0 {
                return Err(ParseError);
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            self.pos += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.pos += 1;
            }
            if self.digits() == 0 {
                return Err(ParseError);
            }
        }
        // The slice is ASCII digits and signs, so it is valid UTF-8.
        let text = std::str::from_utf8(&self.s[start..self.pos]).map_err(|_| ParseError)?;
        Ok(Value::Number(text.to_string()))
    }

    fn hex4(&mut self) -> Result<u32, ParseError> {
        let digits = self.s.get(self.pos..self.pos + 4).ok_or(ParseError)?;
        let mut n = 0u32;
        for &d in digits {
            n = n * 16 + (d as char).to_digit(16).ok_or(ParseError)?;
        }
        self.pos += 4;
        Ok(n)
    }

    fn string(&mut self) -> Result<String, ParseError> {
        self.eat(b'"')?;
        let mut out: Vec<u8> = Vec::new();
        loop {
            let b = self.peek().ok_or(ParseError)?;
            self.pos += 1;
            match b {
                b'"' => break,
                0x00..=0x1F => return Err(ParseError),
                b'\\' => {
                    let e = self.peek().ok_or(ParseError)?;
                    self.pos += 1;
                    let c = match e {
                        b'"' => '"',
                        b'\\' => '\\',
                        b'/' => '/',
                        b'b' => '\u{8}',
                        b'f' => '\u{c}',
                        b'n' => '\n',
                        b'r' => '\r',
                        b't' => '\t',
                        b'u' => self.unicode_escape()?,
                        _ => return Err(ParseError),
                    };
                    let mut buf = [0u8; 4];
                    out.extend_from_slice(c.encode_utf8(&mut buf).as_bytes());
                }
                _ => out.push(b),
            }
        }
        // The input was valid UTF-8 and escapes add whole characters, so this cannot fail; it is still checked.
        String::from_utf8(out).map_err(|_| ParseError)
    }

    fn unicode_escape(&mut self) -> Result<char, ParseError> {
        let first = self.hex4()?;
        let code = match first {
            0xD800..=0xDBFF => {
                self.eat(b'\\')?;
                self.eat(b'u')?;
                let low = self.hex4()?;
                if !(0xDC00..=0xDFFF).contains(&low) {
                    return Err(ParseError);
                }
                0x10000 + ((first - 0xD800) << 10) + (low - 0xDC00)
            }
            0xDC00..=0xDFFF => return Err(ParseError),
            _ => first,
        };
        char::from_u32(code).ok_or(ParseError)
    }
}

/// Two-space indented text ending in a newline. `parse(write_pretty(v)) == v` for every `v` that `parse` returns.
pub fn write_pretty(value: &Value) -> String {
    let mut out = String::new();
    write_value(&mut out, value, 0);
    out.push('\n');
    out
}

fn indent(out: &mut String, level: usize) {
    for _ in 0..level {
        out.push_str("  ");
    }
}

fn write_value(out: &mut String, value: &Value, level: usize) {
    match value {
        Value::Null => out.push_str("null"),
        Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
        Value::Number(n) => out.push_str(n),
        Value::String(s) => write_string(out, s),
        Value::Array(items) if items.is_empty() => out.push_str("[]"),
        Value::Array(items) => {
            out.push_str("[\n");
            for (i, item) in items.iter().enumerate() {
                indent(out, level + 1);
                write_value(out, item, level + 1);
                out.push_str(if i + 1 < items.len() { ",\n" } else { "\n" });
            }
            indent(out, level);
            out.push(']');
        }
        Value::Object(members) if members.is_empty() => out.push_str("{}"),
        Value::Object(members) => {
            out.push_str("{\n");
            for (i, (k, v)) in members.iter().enumerate() {
                indent(out, level + 1);
                write_string(out, k);
                out.push_str(": ");
                write_value(out, v, level + 1);
                out.push_str(if i + 1 < members.len() { ",\n" } else { "\n" });
            }
            indent(out, level);
            out.push('}');
        }
    }
}

fn write_string(out: &mut String, s: &str) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

#[cfg(test)]
mod tests {
    use super::*;

    fn num(s: &str) -> Value {
        Value::Number(s.to_string())
    }
    fn st(s: &str) -> Value {
        Value::String(s.to_string())
    }

    #[test]
    fn reads_an_object_keeping_key_order_and_number_text() {
        let v = parse(br#"{"b": 1.50, "a": [true, false, null], "c": {"d": -0}}"#).unwrap();
        assert_eq!(
            v,
            Value::Object(vec![
                ("b".into(), num("1.50")),
                ("a".into(), Value::Array(vec![Value::Bool(true), Value::Bool(false), Value::Null])),
                ("c".into(), Value::Object(vec![("d".into(), num("-0"))])),
            ])
        );
    }

    #[test]
    fn skips_a_utf8_byte_order_mark_and_surrounding_whitespace() {
        assert_eq!(parse(b"\xEF\xBB\xBF \r\n\t{}\n").unwrap(), Value::Object(vec![]));
    }

    #[test]
    fn decodes_string_escapes_including_surrogate_pairs() {
        let v = parse(br#""a\"b\\c\/d\b\f\n\r\t\u00e9\ud83d\ude00""#).unwrap();
        assert_eq!(v, st("a\"b\\c/d\u{8}\u{c}\n\r\té😀"));
    }

    #[test]
    fn keeps_utf8_text_as_is() {
        assert_eq!(parse("\"juego ñ.exe\"".as_bytes()).unwrap(), st("juego ñ.exe"));
    }

    #[test]
    fn refuses_text_that_is_not_strict_json() {
        let bad: &[&[u8]] = &[
            b"",
            b"   ",
            b"{",
            b"{}}",
            b"{} {}",
            b"{\"a\":1,}",
            b"[1,]",
            b"{\"a\" 1}",
            b"{a:1}",
            b"{'a':1}",
            b"// c\n{}",
            b"{\"a\":01}",
            b"{\"a\":1.}",
            b"{\"a\":.5}",
            b"{\"a\":+1}",
            b"{\"a\":1e}",
            b"{\"a\":-}",
            b"{\"a\":NaN}",
            b"{\"a\":Infinity}",
            b"{\"a\":tru}",
            b"{\"a\":nulls}",
            b"\"\\x\"",
            b"\"\\u12\"",
            b"\"\\ud83d\"",
            b"\"\\ude00\"",
            b"\"a\nb\"",
            b"\"\x01\"",
            b"\"\xff\"",
            b"\"\xc3\"",
            b"\"abc",
            b"{\"a\":1,\"a\":2}",
            b"\xEF\xBB",
            b"\xEF\xBB\xBF\xEF\xBB\xBF{}",
        ];
        for b in bad {
            assert_eq!(parse(b), Err(ParseError), "accepted {:?}", String::from_utf8_lossy(b));
        }
    }

    #[test]
    fn refuses_nesting_deeper_than_the_limit() {
        let ok = format!("{}{}", "[".repeat(MAX_DEPTH), "]".repeat(MAX_DEPTH));
        assert!(parse(ok.as_bytes()).is_ok());
        let deep = format!("{}{}", "[".repeat(MAX_DEPTH + 1), "]".repeat(MAX_DEPTH + 1));
        assert_eq!(parse(deep.as_bytes()), Err(ParseError));
        let very_deep = "[".repeat(1_000_000);
        assert_eq!(parse(very_deep.as_bytes()), Err(ParseError));
    }

    #[test]
    fn checking_for_repeated_keys_grows_linearly_with_the_number_of_keys() {
        fn best_of_three(keys: usize) -> std::time::Duration {
            let text = format!("{{{}}}", (0..keys).map(|i| format!("\"k{i}\":0")).collect::<Vec<_>>().join(","));
            (0..3)
                .map(|_| {
                    let start = std::time::Instant::now();
                    assert!(parse(text.as_bytes()).is_ok());
                    start.elapsed()
                })
                .min()
                .unwrap()
        }
        let small = best_of_three(10_000);
        let large = best_of_three(40_000);
        assert!(large < small * 10, "4x the keys took {:?} vs {:?}", large, small);
    }

    #[test]
    fn accepts_every_number_form_of_the_grammar() {
        for n in ["0", "-0", "12", "-12", "0.5", "1.25e10", "1E+2", "1e-2", "123456789012345678901234567890"] {
            assert_eq!(parse(n.as_bytes()).unwrap(), num(n));
        }
    }

    #[test]
    fn writes_indented_text_with_escapes() {
        let v = Value::Object(vec![
            ("a".into(), num("1")),
            ("b".into(), Value::Array(vec![st("x\"y\\\n\u{1}"), Value::Null])),
            ("c".into(), Value::Object(vec![])),
            ("d".into(), Value::Array(vec![])),
        ]);
        assert_eq!(
            write_pretty(&v),
            "{\n  \"a\": 1,\n  \"b\": [\n    \"x\\\"y\\\\\\n\\u0001\",\n    null\n  ],\n  \"c\": {},\n  \"d\": []\n}\n"
        );
    }

    #[test]
    fn writes_nested_levels_with_their_own_indent_and_leaves_spaces_as_they_are() {
        let v = Value::Object(vec![(
            "a b".into(),
            Value::Array(vec![Value::Object(vec![("c".into(), Value::Array(vec![num("1")]))])]),
        )]);
        assert_eq!(
            write_pretty(&v),
            "{\n  \"a b\": [\n    {\n      \"c\": [\n        1\n      ]\n    }\n  ]\n}\n"
        );
    }

    mod properties {
        use super::super::*;
        use proptest::prelude::*;

        fn any_value() -> impl Strategy<Value = Value> {
            let leaf = prop_oneof![
                Just(Value::Null),
                any::<bool>().prop_map(Value::Bool),
                any::<i64>().prop_map(|n| Value::Number(n.to_string())),
                (any::<i32>(), 0u32..1000, -30i32..30).prop_map(|(i, f, e)| Value::Number(format!("{i}.{f}e{e}"))),
                any::<String>().prop_map(Value::String),
            ];
            leaf.prop_recursive(6, 64, 6, |inner| {
                prop_oneof![
                    prop::collection::vec(inner.clone(), 0..6).prop_map(Value::Array),
                    prop::collection::btree_map(any::<String>(), inner, 0..6)
                        .prop_map(|m| Value::Object(m.into_iter().collect())),
                ]
            })
        }

        proptest! {
            #[test]
            fn what_is_written_reads_back_the_same(v in any_value()) {
                prop_assert_eq!(parse(write_pretty(&v).as_bytes()), Ok(v));
            }

            #[test]
            fn never_panics_on_any_bytes(bytes in prop::collection::vec(any::<u8>(), 0..256)) {
                let _ = parse(&bytes);
            }

            #[test]
            fn never_panics_on_json_like_text(s in "[\\[\\]{}\":,0-9eE.+\\-a-z\\\\ ]{0,64}") {
                let _ = parse(s.as_bytes());
            }
        }
    }
}
