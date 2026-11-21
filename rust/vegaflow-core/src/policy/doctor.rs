//! `--doctor` rules: is the disk big enough for the shader cache, is the cache itself in a sane state, and is a
//! manufacturer tool running that fights VegaFlow. Pure decisions over plain data; the probes are in C++.

const GIB: u64 = 1024 * 1024 * 1024;
/// Under this much free space the disk is `Low`; under `CRITICAL_FREE_BYTES` the shader cache cannot grow any more.
const LOW_FREE_BYTES: u64 = 20 * GIB;
const CRITICAL_FREE_BYTES: u64 = 5 * GIB;
/// A cache over this size is worth deleting (the driver builds it again).
const BIG_CACHE_BYTES: u64 = 10 * GIB;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DiskAdvice {
    /// The probe failed (total of 0 or free above total): nothing can be said.
    Unknown = 0,
    Ok = 1,
    Low = 2,
    Critical = 3,
}

/// `Low` below 20 GiB free or below 10 % free; `Critical` below 5 GiB free. Both limits are strict: exactly 20 GiB free
/// on a 100 GiB disk is `Ok`.
pub fn disk_advice(free_bytes: u64, total_bytes: u64) -> DiskAdvice {
    if total_bytes == 0 || free_bytes > total_bytes {
        return DiskAdvice::Unknown;
    }
    if free_bytes < CRITICAL_FREE_BYTES {
        DiskAdvice::Critical
    } else if free_bytes < LOW_FREE_BYTES || (free_bytes as u128) * 10 < total_bytes as u128 {
        DiskAdvice::Low
    } else {
        DiskAdvice::Ok
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CacheKind {
    Amd,
    Nvidia,
    /// The Windows D3D shader cache, the same for every vendor.
    Generic,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CacheDir {
    pub kind: CacheKind,
    pub exists: bool,
    pub bytes: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Severity {
    Info = 0,
    Warn = 1,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CacheCode {
    NoCacheDirs = 1,
    VendorCacheMissing = 2,
    EmptyCacheLowDisk = 3,
    CacheOver10Gib = 4,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Finding {
    pub severity: Severity,
    pub code: CacheCode,
}

pub fn shader_cache_advice(dirs: &[CacheDir], disk: DiskAdvice) -> Vec<Finding> {
    let mut out = Vec::new();
    if dirs.is_empty() {
        out.push(Finding { severity: Severity::Info, code: CacheCode::NoCacheDirs });
        return out;
    }
    if !dirs.iter().any(|d| d.exists && d.kind != CacheKind::Generic) {
        out.push(Finding { severity: Severity::Warn, code: CacheCode::VendorCacheMissing });
    }
    let mut present = dirs.iter().filter(|d| d.exists).peekable();
    if disk == DiskAdvice::Critical && present.peek().is_some() && present.all(|d| d.bytes == 0) {
        out.push(Finding { severity: Severity::Warn, code: CacheCode::EmptyCacheLowDisk });
    }
    if dirs.iter().any(|d| d.exists && d.bytes > BIG_CACHE_BYTES) {
        out.push(Finding { severity: Severity::Info, code: CacheCode::CacheOver10Gib });
    }
    out
}

pub struct OemTool {
    /// Lower-case executable names.
    pub exes: &'static [&'static str],
    pub product: &'static str,
    /// What it overwrites that VegaFlow also sets.
    pub conflicts: &'static str,
}

// Names are the ones the products ship at the time of writing; vendors rename them between versions.
pub const OEM_TOOLS: &[OemTool] = &[
    OemTool {
        exes: &["armourycrate.exe", "armourycrate.service.exe", "armourycrate.usersessionhelper.exe", "armourycratecontrolinterface.exe"],
        product: "ASUS Armoury Crate",
        conflicts: "power plan and fan curves",
    },
    OemTool { exes: &["lenovovantage.exe", "lenovovantageservice.exe"], product: "Lenovo Vantage", conflicts: "power plan and fan mode" },
    OemTool { exes: &["legionzone.exe", "legionzoneservice.exe"], product: "Lenovo Legion Zone", conflicts: "power plan, GPU limits and fan curves" },
    OemTool {
        exes: &["msicenter.exe", "msi center.exe", "msi.centralserver.exe", "msi_central_service.exe"],
        product: "MSI Center",
        conflicts: "power plan and fan curves",
    },
    OemTool { exes: &["dragoncenter.exe", "dragoncenter_service.exe"], product: "MSI Dragon Center", conflicts: "power plan and fan curves" },
    OemTool { exes: &["omengaminghub.exe", "omencommandcenterbackground.exe"], product: "HP Omen Gaming Hub", conflicts: "power plan and fan curves" },
    OemTool { exes: &["msiafterburner.exe"], product: "MSI Afterburner", conflicts: "GPU clock and fan curves" },
    OemTool { exes: &["rtss.exe"], product: "RTSS", conflicts: "frame limiter and overlay hooks in the game" },
    OemTool {
        exes: &["amdryzenmaster.exe", "ryzenmaster.exe", "amd ryzen master.exe"],
        product: "AMD Ryzen Master",
        conflicts: "CPU power limits and power plan",
    },
];

/// The file name of a process name or path, lower case, with either kind of separator.
fn base_name_lower(s: &str) -> String {
    s.rsplit(['\\', '/']).next().unwrap_or("").trim().to_ascii_lowercase()
}

/// Indexes into `OEM_TOOLS` of the tools that have at least one process in `names`, each once, in table order.
pub fn oem_conflicts(names: &[&str]) -> Vec<usize> {
    let bases: Vec<String> = names.iter().map(|n| base_name_lower(n)).collect();
    (0..OEM_TOOLS.len()).filter(|&i| bases.iter().any(|b| OEM_TOOLS[i].exes.contains(&b.as_str()))).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    const VECTORS: &str = include_str!("../../../../tests/vectors/doctor.tsv");

    fn disk_name(a: DiskAdvice) -> &'static str {
        match a {
            DiskAdvice::Unknown => "unknown",
            DiskAdvice::Ok => "ok",
            DiskAdvice::Low => "low",
            DiskAdvice::Critical => "critical",
        }
    }

    fn code_name(c: CacheCode) -> &'static str {
        match c {
            CacheCode::NoCacheDirs => "no_cache_dirs",
            CacheCode::VendorCacheMissing => "vendor_cache_missing",
            CacheCode::EmptyCacheLowDisk => "empty_cache_low_disk",
            CacheCode::CacheOver10Gib => "cache_over_10gib",
        }
    }

    fn parse_disk(s: &str) -> DiskAdvice {
        [DiskAdvice::Unknown, DiskAdvice::Ok, DiskAdvice::Low, DiskAdvice::Critical]
            .into_iter()
            .find(|d| disk_name(*d) == s)
            .unwrap_or_else(|| panic!("bad disk word {s}"))
    }

    fn parse_dirs(s: &str) -> Vec<CacheDir> {
        if s == "-" {
            return Vec::new();
        }
        s.split(';')
            .map(|d| {
                let p: Vec<&str> = d.split(':').collect();
                let kind = match p[0] {
                    "a" => CacheKind::Amd,
                    "n" => CacheKind::Nvidia,
                    _ => CacheKind::Generic,
                };
                CacheDir { kind, exists: p[1] == "1", bytes: p[2].parse().unwrap() }
            })
            .collect()
    }

    #[test]
    fn shared_vectors_match() {
        let (mut disks, mut caches, mut oems) = (0, 0, 0);
        for line in VECTORS.lines().filter(|l| !l.trim().is_empty() && !l.starts_with('#')) {
            let c: Vec<&str> = line.split('\t').collect();
            match c[0] {
                "disk" => {
                    let got = disk_name(disk_advice(c[2].parse().unwrap(), c[3].parse().unwrap()));
                    assert_eq!(got, c[4], "disk: {}", c[1]);
                    disks += 1;
                }
                "cache" => {
                    let got: Vec<&str> = shader_cache_advice(&parse_dirs(c[3]), parse_disk(c[2])).iter().map(|f| code_name(f.code)).collect();
                    let want: Vec<&str> = if c[4] == "-" { vec![] } else { c[4].split(',').collect() };
                    assert_eq!(got, want, "cache: {}", c[1]);
                    caches += 1;
                }
                "oem" => {
                    let names: Vec<&str> = if c[2].is_empty() { vec![] } else { c[2].split('|').collect() };
                    let mut got: Vec<&str> = oem_conflicts(&names).into_iter().map(|i| OEM_TOOLS[i].product).collect();
                    got.sort();
                    let want: Vec<&str> = if c[3] == "-" { vec![] } else { c[3].split('|').collect() };
                    assert_eq!(got, want, "oem: {}", c[1]);
                    oems += 1;
                }
                other => panic!("unknown row kind {other}"),
            }
        }
        assert!(disks >= 9 && caches >= 8 && oems >= 9, "vectors not read: {disks} {caches} {oems}");
    }

    #[test]
    fn armoury_crate_conflict_names_the_power_plan() {
        let hit = oem_conflicts(&["ArmouryCrate.exe"]);
        assert_eq!(hit.len(), 1);
        assert!(OEM_TOOLS[hit[0]].conflicts.contains("power plan"));
    }

    #[test]
    fn every_oem_exe_is_lower_case_and_unique() {
        let mut seen = std::collections::HashSet::new();
        for t in OEM_TOOLS {
            for e in t.exes {
                assert_eq!(*e, e.to_ascii_lowercase());
                assert!(seen.insert(*e), "duplicate {e}");
            }
        }
    }
}
