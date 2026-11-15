//! Behavior tests that pin `policy::ini_merge` (written against surviving cargo-mutants mutants).

use vegaflow_core::policy::ini_merge::*;

// The `#k` and `;k` tests use artificial keys that start with a comment character. Production never sends such
// keys; they are the only input where "a comment line is not a managed key" can be observed, so they pin that rule.

fn s(bytes: Vec<u8>) -> String {
    String::from_utf8(bytes).unwrap()
}

fn kv(k: &str, v: &str) -> (String, String) {
    (k.to_string(), v.to_string())
}

// --- remove_unreal_uma_block ---

#[test]
fn removing_the_block_keeps_every_line_after_it() {
    let src = b"a=1\n; [VegaFlowUMA_Start]\nx=9\n; [VegaFlowUMA_End]\nb=2\nc=3\n";
    let (out, changed) = remove_unreal_uma_block(src).unwrap();
    assert!(changed);
    assert_eq!(s(out), "a=1\nb=2\nc=3\n");
}

#[test]
fn removing_the_block_drops_exactly_one_blank_separator_before_it_and_keeps_the_tail() {
    let src = b"a=1\n\n\n; [VegaFlowUMA_Start]\nx=9\n; [VegaFlowUMA_End]\nb=2\n";
    let (out, changed) = remove_unreal_uma_block(src).unwrap();
    assert!(changed);
    assert_eq!(s(out), "a=1\n\nb=2\n");
}

#[test]
fn removing_two_blocks_keeps_the_text_between_and_after_them() {
    let src = b"a=1\n; [VegaFlowUMA_Start]\nx\n; [VegaFlowUMA_End]\nm=5\n; [VegaFlowUMA_Start]\ny\n; [VegaFlowUMA_End]\nz=6\n";
    let (out, changed) = remove_unreal_uma_block(src).unwrap();
    assert!(changed);
    assert_eq!(s(out), "a=1\nm=5\nz=6\n");
}

// --- merge_unity_boot_config: comment lines are never managed keys ---

#[test]
fn unity_hash_comment_is_not_a_managed_key() {
    let out = merge_unity_boot_config(b"#a=1\n", &[("#a", "5")]).unwrap();
    assert_eq!(s(out), "#a=1\n#a=5\n");
}

#[test]
fn unity_semicolon_comment_is_not_a_managed_key() {
    let out = merge_unity_boot_config(b";a=1\n", &[(";a", "5")]).unwrap();
    assert_eq!(s(out), ";a=1\n;a=5\n");
}

// --- parse_section_header (through query_ini_section_keys) ---

#[test]
fn a_value_that_is_a_bracketed_word_stays_a_value() {
    let got = query_ini_section_keys(b"[S]\na=[x]\nb=2\n", "S").unwrap();
    assert_eq!(got, vec![kv("a", "[x]"), kv("b", "2")]);
}

#[test]
fn an_unterminated_bracket_line_is_not_a_section_header() {
    // "[Tab" would read as section "Ta" if only the opening bracket were checked.
    let got = query_ini_section_keys(b"[Tab\nk=1\n", "Ta").unwrap();
    assert_eq!(got, Vec::<(String, String)>::new());
}

#[test]
fn a_closing_bracket_without_opening_is_not_a_section_header() {
    // "abc]" would read as section "b" if only the closing bracket were checked.
    let got = query_ini_section_keys(b"abc]\nk=1\n", "b").unwrap();
    assert_eq!(got, Vec::<(String, String)>::new());
}

#[test]
fn a_lone_bracket_is_not_a_section_header() {
    let got = query_ini_section_keys(b"[S]\nk=1\n[\nj=2\n", "S").unwrap();
    assert_eq!(got, vec![kv("k", "1"), kv("j", "2")]);
}

// --- query_ini_section_keys ---

#[test]
fn query_ignores_keys_outside_the_section_comments_and_blank_lines() {
    let src = b"o=0\n[A]\nx=1\n\n; c=2\n# d=3\n  e = 4 \n[B]\ny=5\n";
    assert_eq!(
        query_ini_section_keys(src, "a").unwrap(),
        vec![kv("x", "1"), kv("e", "4")]
    );
    assert_eq!(query_ini_section_keys(src, "B").unwrap(), vec![kv("y", "5")]);
}

#[test]
fn query_never_returns_comment_lines_even_if_they_contain_equals() {
    let got = query_ini_section_keys(b"[A]\n;k=1\n#k=2\n", "A").unwrap();
    assert_eq!(got, Vec::<(String, String)>::new());
}

#[test]
fn query_before_any_section_returns_nothing_for_sections() {
    let got = query_ini_section_keys(b"k=1\n[A]\nj=2\n", "A").unwrap();
    assert_eq!(got, vec![kv("j", "2")]);
}

// --- apply_ini_section_keys ---

#[test]
fn apply_replaces_a_key_in_place_and_keeps_the_neighbours() {
    let out = apply_ini_section_keys(b"[S]\nk=1\nother=2\n", "S", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "[S]\nk=9\nother=2\n");
}

#[test]
fn apply_drops_a_duplicate_of_a_managed_key() {
    let out = apply_ini_section_keys(b"[S]\nk=1\nk=2\nz=3\n", "S", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "[S]\nk=9\nz=3\n");
}

#[test]
fn apply_leaves_the_same_key_in_another_section_alone() {
    let src = b"[Other]\nk=1\n[T]\nk=2\n[After]\nk=3\n";
    let out = apply_ini_section_keys(src, "T", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "[Other]\nk=1\n[T]\nk=9\n[After]\nk=3\n");
}

#[test]
fn apply_leaves_the_same_key_before_any_section_alone() {
    let out = apply_ini_section_keys(b"k=1\n[T]\nj=2\n", "T", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "k=1\n[T]\nj=2\nk=9\n");
}

#[test]
fn apply_hash_comment_is_not_a_managed_key_in_the_target_section() {
    let out = apply_ini_section_keys(b"[T]\n#k=1\n", "T", &[("#k", "9")]).unwrap();
    assert_eq!(s(out), "[T]\n#k=1\n#k=9\n");
}

#[test]
fn apply_semicolon_comment_is_not_a_managed_key_in_the_target_section() {
    let out = apply_ini_section_keys(b"[T]\n;k=1\n", "T", &[(";k", "9")]).unwrap();
    assert_eq!(s(out), "[T]\n;k=1\n;k=9\n");
}

#[test]
fn apply_missing_key_is_added_at_the_end_of_its_section() {
    let out = apply_ini_section_keys(b"[T]\na=1\n[U]\nb=2\n", "T", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "[T]\na=1\nk=9\n[U]\nb=2\n");
}

#[test]
fn apply_new_section_after_a_nonblank_last_line_adds_one_blank_separator() {
    let out = apply_ini_section_keys(b"a=1\n", "S", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "a=1\n\n[S]\nk=9\n");
}

#[test]
fn apply_new_section_after_a_blank_last_line_adds_no_extra_blank() {
    let out = apply_ini_section_keys(b"a=1\n\n", "S", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "a=1\n\n[S]\nk=9\n");
}

#[test]
fn apply_new_section_in_an_empty_file_has_no_leading_blank() {
    let out = apply_ini_section_keys(b"", "S", &[("k", "9")]).unwrap();
    assert_eq!(s(out), "[S]\nk=9\n");
}

// --- remove_ini_section_keys ---

#[test]
fn remove_deletes_only_the_listed_keys_of_the_section() {
    let src = b"[Other]\nk=1\n[T]\nk=2\nkeep=3\n; k=4\n[After]\nk=5\n";
    let (out, changed) = remove_ini_section_keys(src, "T", &["k"]).unwrap();
    assert!(changed);
    assert_eq!(s(out), "[Other]\nk=1\n[T]\nkeep=3\n; k=4\n[After]\nk=5\n");
}

#[test]
fn remove_is_case_insensitive_and_returns_the_new_bytes() {
    let (out, changed) = remove_ini_section_keys(b"[t]\nKEY = 1\nb=2\n", "T", &["key"]).unwrap();
    assert!(changed);
    assert_eq!(out, b"[t]\nb=2\n".to_vec());
}

#[test]
fn remove_without_a_match_returns_the_original_bytes_and_false() {
    let src = b"[T]\na=1\n[U]\nk=2\n";
    let (out, changed) = remove_ini_section_keys(src, "T", &["k"]).unwrap();
    assert!(!changed);
    assert_eq!(out, src.to_vec());
}

#[test]
fn remove_with_no_match_round_trips_a_utf16_file_byte_for_byte() {
    let mut src = vec![0xFF, 0xFE];
    for u in "[T]\r\na=1\r\n".encode_utf16() {
        src.extend_from_slice(&u.to_le_bytes());
    }
    let (out, changed) = remove_ini_section_keys(&src, "T", &["k"]).unwrap();
    assert!(!changed);
    assert_eq!(out, src);
}

#[test]
fn remove_hash_comment_is_not_a_removable_key() {
    let src = b"[T]\n#k=1\n";
    let (out, changed) = remove_ini_section_keys(src, "T", &["#k"]).unwrap();
    assert!(!changed);
    assert_eq!(out, src.to_vec());
}

#[test]
fn remove_semicolon_comment_is_not_a_removable_key() {
    let src = b"[T]\n;k=1\n";
    let (out, changed) = remove_ini_section_keys(src, "T", &[";k"]).unwrap();
    assert!(!changed);
    assert_eq!(out, src.to_vec());
}

#[test]
fn remove_leaves_a_same_named_key_before_any_section() {
    let src = b"k=1\n[T]\nj=2\n";
    let (out, changed) = remove_ini_section_keys(src, "T", &["k"]).unwrap();
    assert!(!changed);
    assert_eq!(out, src.to_vec());
}

#[test]
fn remove_rejects_a_blank_section_name() {
    assert_eq!(remove_ini_section_keys(b"[T]\n", " ", &["k"]), Err(IniError::InvalidInput));
}

// --- merge_unreal_uma_block over an existing block ---

const BLOCK: &str =
    "; [VegaFlowUMA_Start]\n[SystemSettings]\nr.Streaming.PoolSize=4096\n; [VegaFlowUMA_End]";

#[test]
fn merging_over_an_existing_block_replaces_it_and_keeps_the_lines_before_and_after() {
    let src = b"a=1\n; [VegaFlowUMA_Start]\nold=1\n; [VegaFlowUMA_End]\nb=2\nc=3\n";
    let out = merge_unreal_uma_block(src, BLOCK).unwrap();
    assert_eq!(
        s(out),
        "a=1\n; [VegaFlowUMA_Start]\n[SystemSettings]\nr.Streaming.PoolSize=4096\n; [VegaFlowUMA_End]\nb=2\nc=3\n"
    );
}

#[test]
fn merging_over_an_existing_block_with_nothing_after_it_adds_nothing() {
    let src = b"a=1\n; [VegaFlowUMA_Start]\nold=1\n; [VegaFlowUMA_End]\n";
    let out = merge_unreal_uma_block(src, BLOCK).unwrap();
    assert_eq!(
        s(out),
        "a=1\n; [VegaFlowUMA_Start]\n[SystemSettings]\nr.Streaming.PoolSize=4096\n; [VegaFlowUMA_End]\n"
    );
}
