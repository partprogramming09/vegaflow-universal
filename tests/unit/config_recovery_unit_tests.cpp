// Unit tests (F4.4): when config.json is corrupt, newer, has a rejected value or cannot be read, the person decides:
// factory values (after a .vegaflow.bak copy) or open the file. Fake file system; nothing reaches the disk.

#include <string>
#include <vector>

#include "session/config.hpp"
#include "session/config_recovery.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

namespace app = vegaflow::app;
const std::wstring kPath = L"C:\\VegaFlow\\config.json";
const std::wstring kBak = kPath + L".vegaflow.bak";
const std::string kCorrupt = "{ \"poll_interval_ms\": 2000, nope";
const std::string kRejected = "{\"version\": 1, \"poll_interval_ms\": \"fast\"}";
const std::string kNewer = "{\"version\": 999, \"my_note\": \"keep\"}";

struct Run {
    std::string sentence;
    int asked = 0;
    app::ConfigRecoveryOffer offer;
    std::vector<std::wstring> opened;
};

Run Resolve(FakeFileSystem& fs, app::ConfigChoice answer, bool trusted = true, bool dry_run = false) {
    Run r;
    app::ConfigStore store(fs, kPath, trusted);
    app::AppConfig cfg = store.Load();
    app::ConfigRecoveryDeps deps;
    deps.ask = [&](const app::ConfigLoadReport&, const app::ConfigRecoveryOffer& o) {
        ++r.asked;
        r.offer = o;
        return answer;
    };
    deps.open_file = [&](const std::wstring& p) {
        r.opened.push_back(p);
        return true;
    };
    r.sentence = app::ResolveConfigProblem(store, cfg, deps, dry_run);
    return r;
}

bool CleanAfter(FakeFileSystem& fs) {
    app::ConfigStore again(fs, kPath, true);
    again.Load();
    return again.Report().CanSave() && again.Report().Warning().empty();
}

} // namespace

void Test142() {
    std::string bad;
    const std::vector<std::pair<const char*, std::string>> broken = {{"corrupt", kCorrupt}, {"rejected", kRejected}, {"newer", kNewer}};

    for (const auto& [name, text] : broken) {
        // Factory values: the original is copied first, then the file is replaced by a clean one.
        FakeFileSystem fs;
        fs.files[kPath] = text;
        const Run r = Resolve(fs, app::ConfigChoice::FactoryReset);
        if (r.asked != 1 || !r.offer.factory_reset || !r.offer.open_file) bad += std::string(" offer-") + name;
        if (fs.files[kBak] != text) bad += std::string(" bak-") + name;
        if (fs.copies.size() != 1 || fs.replaces.size() != 1) bad += std::string(" steps-") + name;
        if (!CleanAfter(fs)) bad += std::string(" not-clean-") + name;
        if (r.sentence.empty()) bad += std::string(" no-sentence-") + name;

        // Open the file: nothing is written, the problem stays.
        FakeFileSystem fo;
        fo.files[kPath] = text;
        const Run o = Resolve(fo, app::ConfigChoice::OpenFile);
        if (o.opened != std::vector<std::wstring>{kPath} || fo.Changes() != 0) bad += std::string(" open-") + name;

        // Dismiss: nothing at all.
        FakeFileSystem fd;
        fd.files[kPath] = text;
        Resolve(fd, app::ConfigChoice::Dismiss);
        if (fd.Changes() != 0 || fd.files[kPath] != text) bad += std::string(" dismiss-") + name;

        // dry_run: the answer is acted on without writing.
        FakeFileSystem fy;
        fy.files[kPath] = text;
        Resolve(fy, app::ConfigChoice::FactoryReset, true, true);
        if (fy.Changes() != 0) bad += std::string(" dry-") + name;
    }

    // A clean or missing file asks nothing.
    FakeFileSystem clean;
    clean.files[kPath] = "{\"version\": 1}";
    FakeFileSystem missing;
    if (Resolve(clean, app::ConfigChoice::FactoryReset).asked != 0 || Resolve(missing, app::ConfigChoice::FactoryReset).asked != 0) bad += " asked-clean";

    // Unreadable (no permission): only "open the file"; a direct reset writes nothing.
    FakeFileSystem locked;
    locked.files[kPath] = kCorrupt;
    locked.unreadable.insert(kPath);
    const Run lk = Resolve(locked, app::ConfigChoice::FactoryReset);
    if (lk.asked != 1 || lk.offer.factory_reset || !lk.offer.open_file || locked.Changes() != 0) bad += " unreadable";
    app::ConfigStore lstore(locked, kPath, true);
    lstore.Load();
    std::string why;
    if (lstore.ResetToFactory(false, &why) || why.empty() || locked.Changes() != 0) bad += " unreadable-direct";

    // Untrusted folder: nothing to choose, nothing written.
    FakeFileSystem untrusted;
    untrusted.files[kPath] = kCorrupt;
    if (Resolve(untrusted, app::ConfigChoice::FactoryReset, false).asked != 0 || untrusted.Changes() != 0) bad += " untrusted";

    // An older backup is never overwritten: the new copy gets the next free name.
    FakeFileSystem old_bak;
    old_bak.files[kPath] = kCorrupt;
    old_bak.files[kBak] = "older good copy";
    Resolve(old_bak, app::ConfigChoice::FactoryReset);
    if (old_bak.files[kBak] != "older good copy" || old_bak.files[kBak + L".1"] != kCorrupt || !CleanAfter(old_bak)) bad += " bak-kept";

    // No copy, no reset: a failed copy leaves the file as it was; a copy that differs is refused too.
    for (int mode = 0; mode < 2; ++mode) {
        FakeFileSystem nf;
        nf.files[kPath] = kCorrupt;
        nf.fail_copy = mode == 0;
        nf.corrupt_copy_target = mode == 1;
        Resolve(nf, app::ConfigChoice::FactoryReset);
        if (!nf.replaces.empty() || nf.files[kPath] != kCorrupt) bad += " no-copy-no-reset" + std::to_string(mode);
    }

    // The write fails after the copy: the file is still the original and still not overwritten by saves.
    FakeFileSystem fr;
    fr.files[kPath] = kCorrupt;
    fr.fail_replace = true;
    const Run frr = Resolve(fr, app::ConfigChoice::FactoryReset);
    if (fr.files[kPath] != kCorrupt || frr.sentence.empty()) bad += " replace-fails";

    RecordResult(bad.empty(), "Test 142: a broken config.json is the person's decision (factory values after a copy, or open the file)",
                 bad.empty() ? "all cases" : bad);
}

} // namespace vftest
