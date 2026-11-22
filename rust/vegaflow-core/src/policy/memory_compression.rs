//! Whether turning Windows memory compression off is a good idea on this machine.
//! Vectors shared with C++ Test 111: `tests/vectors/memory_compression.tsv`.

/// Installed RAM at or below which compression is worth keeping. Windows reports a little less than installed
/// (part goes to the iGPU), so a 16 GB laptop shows under 16384 and the limit is inclusive.
pub const DISCOURAGE_AT_OR_BELOW_MB: u64 = 16_384;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Advice {
    Allowed = 0,
    Discouraged = 1,
    /// The RAM probe failed or gave 0: nothing to judge by.
    Unknown = 2,
}

pub fn compression_off_advice(total_ram_mb: u64, probe_ok: bool) -> Advice {
    if !probe_ok || total_ram_mb == 0 {
        Advice::Unknown
    } else if total_ram_mb <= DISCOURAGE_AT_OR_BELOW_MB {
        Advice::Discouraged
    } else {
        Advice::Allowed
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_limit_is_inclusive_and_extremes_do_not_overflow() {
        assert_eq!(compression_off_advice(16384, true), Advice::Discouraged);
        assert_eq!(compression_off_advice(16385, true), Advice::Allowed);
        assert_eq!(compression_off_advice(0, true), Advice::Unknown);
        assert_eq!(compression_off_advice(u64::MAX, true), Advice::Allowed);
        assert_eq!(compression_off_advice(8192, false), Advice::Unknown);
    }

    #[test]
    fn advice_matches_the_shared_vectors() {
        const VECTORS: &str = include_str!("../../../../tests/vectors/memory_compression.tsv");
        let mut checked = 0;
        for line in VECTORS.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let c: Vec<&str> = line.split('\t').collect();
            assert_eq!(c.len(), 4, "malformed row: {line}");
            let got = compression_off_advice(c[1].parse().unwrap(), c[2] == "1");
            let want = match c[3] {
                "allowed" => Advice::Allowed,
                "discouraged" => Advice::Discouraged,
                "unknown" => Advice::Unknown,
                other => panic!("bad word {other}"),
            };
            assert_eq!(got, want, "{}", c[0]);
            checked += 1;
        }
        assert!(checked >= 10);
    }
}
