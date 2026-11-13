//! Pure INI and Unity boot.config text editing rules (phase 2).
//!
//! Preserves encoding (UTF-8, UTF-8 BOM, UTF-16 LE/BE BOM), line endings (CRLF, LF, mixed),
//! and content outside VegaFlow's block or managed keys. Never deletes user text if a closing
//! block marker is missing.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16LeBom,
    Utf16BeBom,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IniError {
    InvalidInput,
    InvalidEncoding,
    MissingEndMarker,
    UnmatchedEndMarker,
}

pub const VF_INI_OK: u32 = 0;
pub const VF_INI_ERR_INVALID_INPUT: u32 = 1;
pub const VF_INI_ERR_ENCODING: u32 = 2;
pub const VF_INI_ERR_MISSING_END_MARKER: u32 = 3;
pub const VF_INI_ERR_UNMATCHED_END_MARKER: u32 = 4;

impl IniError {
    pub fn code(self) -> u32 {
        match self {
            IniError::InvalidInput => VF_INI_ERR_INVALID_INPUT,
            IniError::InvalidEncoding => VF_INI_ERR_ENCODING,
            IniError::MissingEndMarker => VF_INI_ERR_MISSING_END_MARKER,
            IniError::UnmatchedEndMarker => VF_INI_ERR_UNMATCHED_END_MARKER,
        }
    }
}

pub const UNREAL_START_MARKER: &str = "; [VegaFlowUMA_Start]";
pub const UNREAL_END_MARKER: &str = "; [VegaFlowUMA_End]";

pub fn decode_text(bytes: &[u8]) -> Result<(TextEncoding, String), IniError> {
    if bytes.starts_with(&[0xEF, 0xBB, 0xBF]) {
        let text = std::str::from_utf8(&bytes[3..]).map_err(|_| IniError::InvalidEncoding)?;
        return Ok((TextEncoding::Utf8Bom, text.to_string()));
    }
    if bytes.starts_with(&[0xFF, 0xFE]) {
        let rest = &bytes[2..];
        if rest.len() % 2 != 0 {
            return Err(IniError::InvalidEncoding);
        }
        let units: Vec<u16> = rest
            .chunks_exact(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        let text = String::from_utf16(&units).map_err(|_| IniError::InvalidEncoding)?;
        return Ok((TextEncoding::Utf16LeBom, text));
    }
    if bytes.starts_with(&[0xFE, 0xFF]) {
        let rest = &bytes[2..];
        if rest.len() % 2 != 0 {
            return Err(IniError::InvalidEncoding);
        }
        let units: Vec<u16> = rest
            .chunks_exact(2)
            .map(|c| u16::from_be_bytes([c[0], c[1]]))
            .collect();
        let text = String::from_utf16(&units).map_err(|_| IniError::InvalidEncoding)?;
        return Ok((TextEncoding::Utf16BeBom, text));
    }
    let text = std::str::from_utf8(bytes).map_err(|_| IniError::InvalidEncoding)?;
    Ok((TextEncoding::Utf8, text.to_string()))
}

pub fn encode_text(enc: TextEncoding, text: &str) -> Vec<u8> {
    match enc {
        TextEncoding::Utf8 => text.as_bytes().to_vec(),
        TextEncoding::Utf8Bom => {
            let mut out = Vec::with_capacity(3 + text.len());
            out.extend_from_slice(&[0xEF, 0xBB, 0xBF]);
            out.extend_from_slice(text.as_bytes());
            out
        }
        TextEncoding::Utf16LeBom => {
            let mut out = Vec::with_capacity(2 + text.len() * 2);
            out.extend_from_slice(&[0xFF, 0xFE]);
            for u in text.encode_utf16() {
                out.extend_from_slice(&u.to_le_bytes());
            }
            out
        }
        TextEncoding::Utf16BeBom => {
            let mut out = Vec::with_capacity(2 + text.len() * 2);
            out.extend_from_slice(&[0xFE, 0xFF]);
            for u in text.encode_utf16() {
                out.extend_from_slice(&u.to_be_bytes());
            }
            out
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct Line {
    content: String,
    eol: &'static str,
}

fn split_lines_preserving_eol(text: &str) -> Vec<Line> {
    let mut lines = Vec::new();
    let bytes = text.as_bytes();
    let mut start = 0;
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'\r' {
            let content = text[start..i].to_string();
            if i + 1 < bytes.len() && bytes[i + 1] == b'\n' {
                lines.push(Line { content, eol: "\r\n" });
                i += 2;
            } else {
                lines.push(Line { content, eol: "\r" });
                i += 1;
            }
            start = i;
        } else if bytes[i] == b'\n' {
            lines.push(Line {
                content: text[start..i].to_string(),
                eol: "\n",
            });
            i += 1;
            start = i;
        } else {
            i += 1;
        }
    }
    if start < bytes.len() {
        lines.push(Line {
            content: text[start..].to_string(),
            eol: "",
        });
    }
    lines
}

fn join_lines(lines: &[Line]) -> String {
    let mut out = String::new();
    for l in lines {
        out.push_str(&l.content);
        out.push_str(l.eol);
    }
    out
}

fn dominant_eol(lines: &[Line]) -> &'static str {
    let mut crlf = 0usize;
    let mut lf = 0usize;
    for l in lines {
        match l.eol {
            "\r\n" => crlf += 1,
            "\n" => lf += 1,
            _ => {}
        }
    }
    if crlf > lf {
        "\r\n"
    } else {
        "\n"
    }
}

fn find_marker_blocks(lines: &[Line]) -> Result<Vec<(usize, usize)>, IniError> {
    let mut blocks = Vec::new();
    let mut current_start: Option<usize> = None;
    for (idx, line) in lines.iter().enumerate() {
        let trimmed = line.content.trim();
        if trimmed == UNREAL_START_MARKER {
            if current_start.is_some() {
                return Err(IniError::MissingEndMarker);
            }
            current_start = Some(idx);
        } else if trimmed == UNREAL_END_MARKER {
            match current_start.take() {
                Some(s) => blocks.push((s, idx)),
                None => return Err(IniError::UnmatchedEndMarker),
            }
        }
    }
    if current_start.is_some() {
        return Err(IniError::MissingEndMarker);
    }
    Ok(blocks)
}

fn parse_block_lines(block_text: &str, eol: &'static str) -> Vec<Line> {
    let mut raw: Vec<&str> = block_text
        .split('\n')
        .map(|s| s.strip_suffix('\r').unwrap_or(s))
        .collect();
    while raw.first().is_some_and(|s| s.trim().is_empty()) {
        raw.remove(0);
    }
    while raw.last().is_some_and(|s| s.trim().is_empty()) {
        raw.pop();
    }
    raw.into_iter()
        .map(|s| Line {
            content: s.to_string(),
            eol,
        })
        .collect()
}

pub fn merge_unreal_uma_block(original: &[u8], block_text: &str) -> Result<Vec<u8>, IniError> {
    let (enc, text) = decode_text(original)?;
    let lines = split_lines_preserving_eol(&text);
    let blocks = find_marker_blocks(&lines)?;
    let eol = dominant_eol(&lines);
    let new_block = parse_block_lines(block_text, eol);

    let mut out = Vec::with_capacity(lines.len() + new_block.len() + 2);
    if blocks.is_empty() {
        out.extend(lines);
        if !out.is_empty() {
            if let Some(last) = out.last_mut() {
                if last.eol.is_empty() {
                    last.eol = eol;
                }
            }
            if !out.last().unwrap().content.trim().is_empty() {
                out.push(Line {
                    content: String::new(),
                    eol,
                });
            }
        }
        out.extend(new_block);
    } else {
        let mut idx = 0usize;
        let mut replaced = false;
        for (start, end) in blocks {
            while idx < start {
                out.push(lines[idx].clone());
                idx += 1;
            }
            if !replaced {
                out.extend(new_block.iter().cloned());
                replaced = true;
            }
            idx = end + 1;
        }
        while idx < lines.len() {
            out.push(lines[idx].clone());
            idx += 1;
        }
    }

    Ok(encode_text(enc, &join_lines(&out)))
}

pub fn remove_unreal_uma_block(original: &[u8]) -> Result<(Vec<u8>, bool), IniError> {
    let (enc, text) = decode_text(original)?;
    let lines = split_lines_preserving_eol(&text);
    let blocks = find_marker_blocks(&lines)?;
    if blocks.is_empty() {
        return Ok((original.to_vec(), false));
    }

    let mut out = Vec::with_capacity(lines.len());
    let mut idx = 0usize;
    for (start, end) in blocks {
        while idx < start {
            out.push(lines[idx].clone());
            idx += 1;
        }
        // Drop a single blank separator line immediately before the removed block if there is one.
        if out.last().is_some_and(|l| l.content.trim().is_empty()) {
            out.pop();
        }
        idx = end + 1;
    }
    while idx < lines.len() {
        out.push(lines[idx].clone());
        idx += 1;
    }

    Ok((encode_text(enc, &join_lines(&out)), true))
}

pub fn merge_unity_boot_config(
    original: &[u8],
    updates: &[(&str, &str)],
) -> Result<Vec<u8>, IniError> {
    let (enc, text) = decode_text(original)?;
    let lines = split_lines_preserving_eol(&text);
    let eol = dominant_eol(&lines);

    let mut seen = vec![false; updates.len()];
    let mut out = Vec::with_capacity(lines.len() + updates.len());

    for line in lines {
        let trimmed = line.content.trim();
        if !trimmed.is_empty() && !trimmed.starts_with('#') && !trimmed.starts_with(';') {
            if let Some(eq) = trimmed.find('=') {
                let key = trimmed[..eq].trim();
                if let Some((pos, &(k, v))) = updates
                    .iter()
                    .enumerate()
                    .find(|(_, (uk, _))| uk.eq_ignore_ascii_case(key))
                {
                    if !seen[pos] {
                        out.push(Line {
                            content: format!("{k}={v}"),
                            eol: line.eol,
                        });
                        seen[pos] = true;
                    }
                    // Duplicate occurrence of an already-applied managed key is dropped.
                    continue;
                }
            }
        }
        out.push(line);
    }

    for (pos, &(k, v)) in updates.iter().enumerate() {
        if !seen[pos] {
            if let Some(last) = out.last_mut() {
                if last.eol.is_empty() {
                    last.eol = eol;
                }
            }
            out.push(Line {
                content: format!("{k}={v}"),
                eol,
            });
        }
    }

    Ok(encode_text(enc, &join_lines(&out)))
}

fn parse_section_header(trimmed: &str) -> Option<&str> {
    if trimmed.starts_with('[') && trimmed.ends_with(']') && trimmed.len() >= 2 {
        Some(&trimmed[1..trimmed.len() - 1])
    } else {
        None
    }
}

pub fn apply_ini_section_keys(
    original: &[u8],
    section: &str,
    updates: &[(&str, &str)],
) -> Result<Vec<u8>, IniError> {
    if section.trim().is_empty() {
        return Err(IniError::InvalidInput);
    }
    let (enc, text) = decode_text(original)?;
    let lines = split_lines_preserving_eol(&text);
    let eol = dominant_eol(&lines);

    let mut seen = vec![false; updates.len()];
    let mut out = Vec::with_capacity(lines.len() + updates.len() + 2);
    let mut in_target = false;
    let mut found_target = false;

    let flush_missing = |out: &mut Vec<Line>, seen: &mut [bool]| {
        for (idx, &(k, v)) in updates.iter().enumerate() {
            if !seen[idx] {
                if let Some(last) = out.last_mut() {
                    if last.eol.is_empty() {
                        last.eol = eol;
                    }
                }
                out.push(Line {
                    content: format!("{k}={v}"),
                    eol,
                });
                seen[idx] = true;
            }
        }
    };

    for line in lines {
        let trimmed = line.content.trim();
        if let Some(sec_name) = parse_section_header(trimmed) {
            if in_target {
                flush_missing(&mut out, &mut seen);
                in_target = false;
            }
            if sec_name.eq_ignore_ascii_case(section) {
                in_target = true;
                found_target = true;
            }
            out.push(line);
            continue;
        }

        if in_target && !trimmed.is_empty() && !trimmed.starts_with(';') && !trimmed.starts_with('#')
        {
            if let Some(eq) = trimmed.find('=') {
                let key = trimmed[..eq].trim();
                if let Some((idx, &(k, v))) = updates
                    .iter()
                    .enumerate()
                    .find(|(_, (uk, _))| uk.eq_ignore_ascii_case(key))
                {
                    if !seen[idx] {
                        out.push(Line {
                            content: format!("{k}={v}"),
                            eol: line.eol,
                        });
                        seen[idx] = true;
                    }
                    continue;
                }
            }
        }
        out.push(line);
    }

    if in_target {
        flush_missing(&mut out, &mut seen);
    }

    if !found_target {
        if !out.is_empty() {
            if let Some(last) = out.last_mut() {
                if last.eol.is_empty() {
                    last.eol = eol;
                }
            }
            if !out.last().unwrap().content.trim().is_empty() {
                out.push(Line {
                    content: String::new(),
                    eol,
                });
            }
        }
        out.push(Line {
            content: format!("[{section}]"),
            eol,
        });
        flush_missing(&mut out, &mut seen);
    }

    Ok(encode_text(enc, &join_lines(&out)))
}

pub fn remove_ini_section_keys(
    original: &[u8],
    section: &str,
    keys: &[&str],
) -> Result<(Vec<u8>, bool), IniError> {
    if section.trim().is_empty() {
        return Err(IniError::InvalidInput);
    }
    let (enc, text) = decode_text(original)?;
    let lines = split_lines_preserving_eol(&text);

    let mut out = Vec::with_capacity(lines.len());
    let mut in_target = false;
    let mut changed = false;

    for line in lines {
        let trimmed = line.content.trim();
        if let Some(sec_name) = parse_section_header(trimmed) {
            in_target = sec_name.eq_ignore_ascii_case(section);
            out.push(line);
            continue;
        }
        if in_target && !trimmed.is_empty() && !trimmed.starts_with(';') && !trimmed.starts_with('#')
        {
            if let Some(eq) = trimmed.find('=') {
                let key = trimmed[..eq].trim();
                if keys.iter().any(|k| k.eq_ignore_ascii_case(key)) {
                    changed = true;
                    continue;
                }
            }
        }
        out.push(line);
    }

    if !changed {
        return Ok((original.to_vec(), false));
    }
    Ok((encode_text(enc, &join_lines(&out)), true))
}

pub fn query_ini_section_keys(
    original: &[u8],
    section: &str,
) -> Result<Vec<(String, String)>, IniError> {
    if section.trim().is_empty() {
        return Err(IniError::InvalidInput);
    }
    let (_enc, text) = decode_text(original)?;
    let lines = split_lines_preserving_eol(&text);

    let mut out = Vec::new();
    let mut in_target = false;

    for line in lines {
        let trimmed = line.content.trim();
        if let Some(sec_name) = parse_section_header(trimmed) {
            in_target = sec_name.eq_ignore_ascii_case(section);
            continue;
        }
        if in_target && !trimmed.is_empty() && !trimmed.starts_with(';') && !trimmed.starts_with('#')
        {
            if let Some(eq) = trimmed.find('=') {
                let key = trimmed[..eq].trim().to_string();
                let val = trimmed[eq + 1..].trim().to_string();
                out.push((key, val));
            }
        }
    }

    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;

    const SAMPLE_BLOCK: &str =
        "; [VegaFlowUMA_Start]\n[SystemSettings]\nr.Streaming.PoolSize=4096\n; [VegaFlowUMA_End]";

    #[test]
    fn missing_end_marker_returns_error_and_does_not_delete_user_content() {
        let raw = b"[Core.System]\nPaths=foo\n\n; [VegaFlowUMA_Start]\n[SystemSettings]\nKeepMe=1\n";
        let err = merge_unreal_uma_block(raw, SAMPLE_BLOCK).unwrap_err();
        assert_eq!(err, IniError::MissingEndMarker);
        let err2 = remove_unreal_uma_block(raw).unwrap_err();
        assert_eq!(err2, IniError::MissingEndMarker);
    }

    #[test]
    fn unmatched_end_marker_returns_error() {
        let raw = b"[Core.System]\n; [VegaFlowUMA_End]\n";
        assert_eq!(
            merge_unreal_uma_block(raw, SAMPLE_BLOCK).unwrap_err(),
            IniError::UnmatchedEndMarker
        );
    }

    #[test]
    fn unreal_block_leaves_keys_outside_block_untouched() {
        let raw = b"[SystemSettings]\nr.ScreenPercentage=100\nr.TemporalAA.Upsampling=1\nr.DefaultFeature.AntiAliasing=2\n";
        let merged = merge_unreal_uma_block(raw, SAMPLE_BLOCK).unwrap();
        let text = String::from_utf8(merged).unwrap();
        assert!(text.contains("r.ScreenPercentage=100\n"));
        assert!(text.contains("r.TemporalAA.Upsampling=1\n"));
        assert!(text.contains("r.DefaultFeature.AntiAliasing=2\n"));
    }

    #[test]
    fn utf16_bom_and_crlf_preserved_and_idempotent() {
        let orig_text = "; Comentario español áéíóú\r\n[Core.System]\r\nPaths=foo\r\n";
        let orig_bytes = encode_text(TextEncoding::Utf16LeBom, orig_text);

        let once = merge_unreal_uma_block(&orig_bytes, SAMPLE_BLOCK).unwrap();
        assert!(once.starts_with(&[0xFF, 0xFE]));
        let (enc, decoded) = decode_text(&once).unwrap();
        assert_eq!(enc, TextEncoding::Utf16LeBom);
        assert!(decoded.contains("; Comentario español áéíóú\r\n"));
        assert!(decoded.contains("; [VegaFlowUMA_Start]\r\n"));

        let twice = merge_unreal_uma_block(&once, SAMPLE_BLOCK).unwrap();
        assert_eq!(once, twice, "applying twice must equal applying once");

        let (restored, changed) = remove_unreal_uma_block(&once).unwrap();
        assert!(changed);
        assert_eq!(restored, orig_bytes);
    }

    #[test]
    fn mixed_line_endings_stay_exact_on_untouched_lines() {
        let raw = b"[Core.System]\r\nPaths=a\n[Other]\r\nKey=1";
        let once = merge_unreal_uma_block(raw, SAMPLE_BLOCK).unwrap();
        assert!(once.starts_with(b"[Core.System]\r\nPaths=a\n[Other]\r\nKey=1"));
        let twice = merge_unreal_uma_block(&once, SAMPLE_BLOCK).unwrap();
        assert_eq!(once, twice);
    }

    #[test]
    fn unity_handles_spaces_around_equals_and_removes_duplicates() {
        let raw = b"gfx-enable-gfx-jobs = 0\r\nother=keep\r\ngfx-enable-gfx-jobs=0\r\n";
        let updates = [("gfx-enable-gfx-jobs", "1"), ("gc-max-time-slice", "3")];
        let once = merge_unity_boot_config(raw, &updates).unwrap();
        let text = String::from_utf8(once.clone()).unwrap();
        assert_eq!(
            text,
            "gfx-enable-gfx-jobs=1\r\nother=keep\r\ngc-max-time-slice=3\r\n"
        );
        let twice = merge_unity_boot_config(&once, &updates).unwrap();
        assert_eq!(once, twice);
    }

    #[test]
    fn shadow_config_recognizes_system_settings_after_utf8_bom() {
        let raw = b"\xef\xbb\xbf[SystemSettings]\nr.TextureStreaming=1\nr.Shadow.CSM.MaxCascades = 4\n";
        let updates = [("r.Shadow.CSM.MaxCascades", "2"), ("r.VolumetricFog", "0")];
        let out = apply_ini_section_keys(raw, "SystemSettings", &updates).unwrap();
        assert!(out.starts_with(&[0xEF, 0xBB, 0xBF]));
        let text = std::str::from_utf8(&out[3..]).unwrap();
        assert_eq!(text.matches("[SystemSettings]").count(), 1);
        assert!(text.contains("r.Shadow.CSM.MaxCascades=2\n"));
        assert!(text.contains("r.VolumetricFog=0\n"));

        let queried = query_ini_section_keys(&out, "SystemSettings").unwrap();
        assert!(queried.iter().any(|(k, v)| k == "r.Shadow.CSM.MaxCascades" && v == "2"));
        assert!(queried.iter().any(|(k, v)| k == "r.VolumetricFog" && v == "0"));
    }

    #[test]
    fn all_ini_vector_files_behave_as_expected() {
        let base = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/vectors/ini");

        for name in ["utf8_lf.ini", "utf8_bom_crlf.ini", "utf16le_bom_crlf.ini", "mixed_eol.ini", "already_applied.ini"] {
            let bytes = std::fs::read(base.join(name)).unwrap();
            let once = merge_unreal_uma_block(&bytes, SAMPLE_BLOCK).unwrap();
            let twice = merge_unreal_uma_block(&once, SAMPLE_BLOCK).unwrap();
            assert_eq!(once, twice, "{name}: idempotency failed");
        }

        let missing = std::fs::read(base.join("missing_end_marker.ini")).unwrap();
        assert_eq!(
            merge_unreal_uma_block(&missing, SAMPLE_BLOCK).unwrap_err(),
            IniError::MissingEndMarker
        );

        let unity = std::fs::read(base.join("unity_spaces_dups.config")).unwrap();
        let u_once = merge_unity_boot_config(&unity, &[("gfx-enable-gfx-jobs", "1"), ("gc-max-time-slice", "3")]).unwrap();
        let u_twice = merge_unity_boot_config(&u_once, &[("gfx-enable-gfx-jobs", "1"), ("gc-max-time-slice", "3")]).unwrap();
        assert_eq!(u_once, u_twice);
        let u_text = String::from_utf8(u_once).unwrap();
        assert_eq!(u_text.matches("gfx-enable-gfx-jobs=").count(), 1);
        assert!(u_text.contains("gfx-enable-gfx-jobs=1"));

        let shadow_bom = std::fs::read(base.join("shadow_bom.ini")).unwrap();
        let s_out = apply_ini_section_keys(&shadow_bom, "SystemSettings", &[("r.Shadow.CSM.MaxCascades", "2")]).unwrap();
        let (_, s_dec) = decode_text(&s_out).unwrap();
        assert_eq!(s_dec.matches("[SystemSettings]").count(), 1);
    }

    proptest! {
        #[test]
        fn prop_arbitrary_bytes_never_panic_on_unreal_or_unity(data in proptest::collection::vec(any::<u8>(), 0..512)) {
            let _ = merge_unreal_uma_block(&data, SAMPLE_BLOCK);
            let _ = remove_unreal_uma_block(&data);
            let _ = merge_unity_boot_config(&data, &[("gfx-enable-gfx-jobs", "1")]);
            let _ = apply_ini_section_keys(&data, "SystemSettings", &[("r.VolumetricFog", "0")]);
            let _ = query_ini_section_keys(&data, "SystemSettings");
        }

        #[test]
        fn prop_valid_utf8_and_utf16_unreal_merges_are_idempotent(
            body in "[a-zA-Z0-9_=\\.\\r\\n\\[\\] ;]{0,200}",
            use_utf16 in any::<bool>(),
            use_bom in any::<bool>(),
        ) {
            prop_assume!(!body.contains("[VegaFlowUMA_Start]") && !body.contains("[VegaFlowUMA_End]"));
            let enc = if use_utf16 {
                TextEncoding::Utf16LeBom
            } else if use_bom {
                TextEncoding::Utf8Bom
            } else {
                TextEncoding::Utf8
            };
            let bytes = encode_text(enc, &body);
            let once = merge_unreal_uma_block(&bytes, SAMPLE_BLOCK).unwrap();
            let twice = merge_unreal_uma_block(&once, SAMPLE_BLOCK).unwrap();
            prop_assert_eq!(once, twice);
        }
    }
}
