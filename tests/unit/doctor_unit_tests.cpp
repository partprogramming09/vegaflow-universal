// Unit tests: `--doctor` (E2.5). Pure: BuildDoctorReport takes the probe results as plain data, so nothing here reads the
// disk, the registry or the process list. The rules themselves are also checked by Rust against tests/vectors/doctor.tsv.

#include "../test_support.hpp"

#include "bridge/rust_bridge.h"
#include "session/doctor.hpp"
#include "session/report.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;

constexpr uint64_t kGiB = 1024ull * 1024 * 1024;

gs::DoctorInputs Healthy() {
    gs::DoctorInputs in;
    in.version = "0.2.0";
    in.elevated = true;
    in.disks.push_back({"System drive", "C:\\", true, 200 * kGiB, 500 * kGiB});
    in.caches.push_back({"C:\\Users\\Juan\\AppData\\Local\\AMD\\DxCache", 1048576, 10, true});
    return in;
}

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

bool JsonOk(const std::string& s) { return vf_rust_doctor_json_is_valid(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

} // namespace

void Test107() {
    // 1. 4 GiB free: critical, exit code 2.
    auto low = Healthy();
    low.disks[0].free_bytes = 4 * kGiB;
    const auto r1 = gs::BuildDoctorReport(low);
    RecordResult(r1.exit_code == 2 && Has(r1.text, "CRITICAL"), "Test 107.1: 4 GiB free is critical and exits with 2", "exit=" + std::to_string(r1.exit_code));

    // 2. exactly 20 GiB free of 100 GiB: ok (edge), exit 0.
    auto edge = Healthy();
    edge.disks[0].free_bytes = 20 * kGiB;
    edge.disks[0].total_bytes = 100 * kGiB;
    const auto r2 = gs::BuildDoctorReport(edge);
    RecordResult(r2.exit_code == 0 && !Has(r2.text, "WARN") && !Has(r2.text, "CRITICAL"), "Test 107.2: exactly 20 GiB free of 100 GiB is ok",
                 r2.text);

    // 3. A failed probe (total 0) says it could not read, and does not divide by zero.
    auto dead = Healthy();
    dead.disks[0].free_bytes = 0;
    dead.disks[0].total_bytes = 0;
    const auto r3 = gs::BuildDoctorReport(dead);
    RecordResult(Has(r3.text, "could not read") && !Has(r3.text, "nan") && !Has(r3.text, "inf"), "Test 107.3: total 0 says 'could not read'", r3.text);

    // 4. No shader cache folders: an informative note, not an error.
    auto none = Healthy();
    none.caches.clear();
    const auto r4 = gs::BuildDoctorReport(none);
    RecordResult(r4.exit_code == 0 && Has(r4.text, "INFO") && Has(r4.text, "no shader cache"), "Test 107.4: no cache folders is an info note, exit 0", r4.text);

    // 5. A 12 GiB cache: suggests deleting it, deletes nothing (the report only reads).
    auto big = Healthy();
    big.caches[0].bytes = 12 * kGiB;
    const auto r5 = gs::BuildDoctorReport(big);
    RecordResult(Has(r5.text, "delete") && Has(r5.text, "rebuilds") && r5.exit_code == 0, "Test 107.5: a 12 GiB cache suggests deleting it", r5.text);

    // 6. ArmouryCrate.exe running: conflict with the power plan.
    auto oem = Healthy();
    oem.processes = {"explorer.exe", "ArmouryCrate.exe"};
    const auto r6 = gs::BuildDoctorReport(oem);
    RecordResult(Has(r6.text, "ASUS Armoury Crate") && Has(r6.text, "power plan") && r6.exit_code == 1, "Test 107.6: ArmouryCrate.exe conflicts with the power plan",
                 r6.text);

    // 7. Upper case or with a path: found just the same.
    auto upper = Healthy();
    upper.processes = {"ARMOURYCRATE.EXE"};
    auto path = Healthy();
    path.processes = {"C:\\Program Files\\ASUS\\ArmouryCrate.exe"};
    RecordResult(Has(gs::BuildDoctorReport(upper).text, "ASUS Armoury Crate") && Has(gs::BuildDoctorReport(path).text, "ASUS Armoury Crate"),
                 "Test 107.7: the process name is found in upper case and with a path");

    // 8. Neither the user name nor the computer name leaves in the text or the JSON.
    auto priv = Healthy();
    priv.computer_name = "DESKTOP-ABC";
    priv.domain_name = "CORP-DOM";
    priv.disks.push_back({"Game drive", "D:\\Users\\Juan P\xC3\xA9rez\\Games on DESKTOP-ABC", true, 100 * kGiB, 500 * kGiB});
    priv.caches.push_back({"C:\\Users\\Juan P\xC3\xA9rez\\AppData\\Local\\D3DSCache", 5, 1, true});
    priv.processes = {"C:\\Users\\Juan P\xC3\xA9rez\\bin\\ArmouryCrate.exe"};
    const auto r8 = gs::BuildDoctorReport(priv);
    bool clean = true;
    for (const std::string* s : {&r8.text, &r8.json}) {
        for (const char* secret : {"Juan", "rez", "DESKTOP-ABC", "CORP-DOM"}) clean = clean && !Has(*s, secret);
    }
    RecordResult(clean && Has(r8.text, "<usuario>") && JsonOk(r8.json), "Test 107.8: user, computer and domain names are hidden in the text and the JSON", r8.text);

    // 9. Twice in a row: the same output (the report is a function of its inputs; the real "writes nothing" is checked on the laptop).
    RecordResult(gs::BuildDoctorReport(priv).text == r8.text && gs::BuildDoctorReport(priv).json == r8.json, "Test 107.9: the same inputs give the same report");

    // 10. Not elevated: it still works and says what it could not read.
    auto user = Healthy();
    user.elevated = false;
    user.caches_probed = false;
    user.caches.clear();
    const auto r10 = gs::BuildDoctorReport(user);
    RecordResult(Has(r10.text, "not elevated") && Has(r10.text, "could not read the shader caches") && r10.exit_code == 0,
                 "Test 107.10: without administrator rights it works and marks what it could not read", r10.text);

    // 11. --json with quotes, backslashes, control characters and accents in the paths: valid JSON (checked by the Rust parser).
    auto odd = Healthy();
    odd.disks.push_back({"Odd \"drive\"", "E:\\a\"b\\c\\\\d\t\x01 \xC3\xA9", true, 50 * kGiB, 500 * kGiB});
    odd.caches.push_back({"C:\\x\"y\\z\nw", 1, 1, true});
    const auto r11 = gs::BuildDoctorReport(odd);
    RecordResult(JsonOk(r11.json) && JsonOk(gs::BuildDoctorReport(Healthy()).json), "Test 107.11: --json is valid JSON with quotes and backslashes in paths", r11.json);

    // 12. One tool, three processes: one conflict.
    auto three = Healthy();
    three.processes = {"ArmouryCrate.exe", "ArmouryCrate.Service.exe", "ArmouryCrate.UserSessionHelper.exe"};
    const auto r12 = gs::BuildDoctorReport(three);
    size_t hits = 0;
    for (size_t p = r12.text.find("ASUS Armoury Crate"); p != std::string::npos; p = r12.text.find("ASUS Armoury Crate", p + 1)) ++hits;
    RecordResult(hits == 1, "Test 107.12: three processes of one tool give one conflict", "hits=" + std::to_string(hits));

    // 13. A line over 200 characters comes out whole, and so does the JSON.
    auto longp = Healthy();
    const std::string long_path = "D:\\" + std::string(400, 'p') + "\\end";
    longp.disks.push_back({"Long", long_path, true, 100 * kGiB, 500 * kGiB});
    const auto r13 = gs::BuildDoctorReport(longp);
    RecordResult(Has(r13.text, long_path) && Has(r13.json, std::string(400, 'p') + "\\\\end") && JsonOk(r13.json),
                 "Test 107.13: a path over 200 characters is not cut");

    // 14. Healthy caches are listed too (kind, size, files), in the text and in the JSON, with the user name hidden; a folder
    // that does not exist is not listed; a cache that could not be read says so.
    auto caches = Healthy();
    caches.caches.clear();
    caches.caches.push_back({"C:\\Users\\Juan\\AppData\\Local\\D3DSCache", 3 * 1024 * 1024, 12, true});
    caches.caches.push_back({"C:\\Users\\Juan\\AppData\\Local\\AMD\\DxCache", 144ull * 1024 * 1024, 340, true});
    caches.caches.push_back({"C:\\Users\\Juan\\AppData\\Local\\NVIDIA\\DXCache", 0, 0, false});
    const auto r16 = gs::BuildDoctorReport(caches);
    RecordResult(Has(r16.text, "Shader caches:") && Has(r16.text, "D3D") && Has(r16.text, "3.0 MiB (12 files)") && Has(r16.text, "AMD") &&
                     Has(r16.text, "144.0 MiB (340 files)") && !Has(r16.text, "NVIDIA") && !Has(r16.text, "Juan") && Has(r16.text, "<usuario>") &&
                     Has(r16.json, "\"shader_caches\":[{\"kind\":\"D3D\"") && Has(r16.json, "\"bytes\":150994944,\"files\":340") &&
                     !Has(r16.json, "Juan") && JsonOk(r16.json),
                 "Test 107.16: healthy shader caches are listed with kind, size and files, in the text and the JSON", r16.text + r16.json);
    auto unread = Healthy();
    unread.caches_probed = false;
    unread.caches.clear();
    const auto r17 = gs::BuildDoctorReport(unread);
    RecordResult(Has(r17.text, "Shader caches:\n  could not read") && Has(r17.json, "\"shader_caches\":[]") && JsonOk(r17.json),
                 "Test 107.17: an unreadable cache probe says so and the JSON list is empty", r17.text + r17.json);

    // RedactReport on its own: keeps line breaks and length, hides the names case-insensitively.
    const std::string red = gs::RedactReport("a\r\nC:\\Users\\Ana\\x on desktop-abc of Corp-Dom\r\n" + std::string(300, 'z'), "DESKTOP-ABC", "CORP-DOM");
    RecordResult(Has(red, "\r\n") && !Has(red, "Ana") && !Has(red, "abc") && !Has(red, "Dom") && red.size() > 300,
                 "Test 107.14: RedactReport keeps line breaks and length and hides the names", red);
    RecordResult(gs::RedactReport("keep C:\\x", "", "") == "keep C:\\x", "Test 107.15: RedactReport with empty names changes nothing else");
}

} // namespace vftest
