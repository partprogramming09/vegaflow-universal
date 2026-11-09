
const ANTI_CHEAT_SIGNATURES: &[&[u8]] = &[
    b"easyanticheat",
    b"battleye",
    b"vanguard",
    b"vgk.sys",
    b"randgrid.sys",
    b"anticheatexpert",
    b"hoyokprotect",
    b"mhyprot",
    b"mhypbase",
    b"acebase.dll",
    b"acebase",
    b"ace-base",
    b"beservice",
    b"blackcipher",
    b"equ8",
    b"nprotect",
    b"gameguard",
    b"xigncode",
    b"genshinimpact",
    b"zenlesszonezero",
    b"starrail",
];

const CRITICAL_SYSTEM_FILES: &[&[u8]] = &[
    b"bootmgr",
    b"pagefile.sys",
    b"hiberfil.sys",
    b"swapfile.sys",
    b"ntldr",
    b"bootsect.bak",
];

#[inline]
fn contains_ignore_case(haystack: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    if needle.len() > haystack.len() {
        return false;
    }
    haystack.windows(needle.len()).any(|window| {
        window
            .iter()
            .zip(needle.iter())
            .all(|(&h, &n)| h.eq_ignore_ascii_case(&n))
    })
}

#[inline]
fn eq_ignore_case(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    a.iter()
        .zip(b.iter())
        .all(|(&x, &y)| x.eq_ignore_ascii_case(&y))
}

#[inline]
fn extract_filename(path: &[u8]) -> &[u8] {
    let mut start = 0;
    for (i, &b) in path.iter().enumerate() {
        if b == b'/' || b == b'\\' {
            start = i + 1;
        }
    }
    &path[start..]
}

pub fn has_anticheat_signature(path_bytes: &[u8]) -> bool {

    for &sig in ANTI_CHEAT_SIGNATURES {
        if contains_ignore_case(path_bytes, sig) {
            return true;
        }
    }

    false
}

pub fn is_critical_system_filename(raw_bytes: &[u8]) -> bool {
    let filename = extract_filename(raw_bytes);

    for &crit in CRITICAL_SYSTEM_FILES {
        if eq_ignore_case(filename, crit) {
            return true;
        }
    }

    false
}
