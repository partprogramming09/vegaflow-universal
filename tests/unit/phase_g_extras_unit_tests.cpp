// Unit tests: the phase G extras that are pure logic: exit notice, panic shortcut, welcome mark, game-pad navigation,
// the "what would happen" preview of a setting and the "Pausar VegaFlow en este juego" offer.

#include "session/exit_notice.hpp"
#include "session/home_screen.hpp"
#include "session/pad_nav.hpp"
#include "session/panic_hotkey.hpp"
#include "session/settings_screen.hpp"
#include "session/welcome.hpp"
#include "../test_support.hpp"
#include "fake_file_system.hpp"

#include <string>

namespace vftest {

namespace {

namespace gs = vegaflow::session;

gs::HistoryRow Row(uint64_t id, const char* state) {
    gs::HistoryRow r;
    r.id = id;
    r.state = state;
    return r;
}

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

} // namespace

void Test85() {
    // Exit notice: only changes in force make the question appear.
    gs::HistoryScreenModel h;
    RecordResult(!gs::BuildExitNotice(h).ask, "Test 85.1: an empty history closes without a question");
    h.rows = {Row(1, "Deshecho"), Row(2, "Falló")};
    RecordResult(!gs::BuildExitNotice(h).ask, "Test 85.2: undone and failed changes do not count");
    h.rows = {Row(1, "Activo"), Row(2, "Deshecho")};
    auto one = gs::BuildExitNotice(h);
    RecordResult(one.ask && one.active == 1 && Has(one.text, "1 cambio puesto") && Has(one.text, "no los deshace"), "Test 85.3: one active change is asked about in the singular and says closing does not undo it", one.text);
    h.rows = {Row(1, "Activo"), Row(2, "Activo"), Row(3, "Activo")};
    RecordResult(Has(gs::BuildExitNotice(h).text, "3 cambios puestos"), "Test 85.4: several active changes are counted");

    // Panic shortcut: never a bare key or a single modifier.
    const auto def = gs::DefaultPanicHotkey();
    RecordResult(gs::IsSafePanicHotkey(def) && def.label == "Ctrl+Alt+Mayús+R", "Test 85.5: the default panic shortcut is safe and labelled", def.label);
    gs::PanicHotkey bare{false, false, false, 'R', "R"};
    gs::PanicHotkey single{false, false, true, 'R', "Mayús+R"};
    gs::PanicHotkey odd_key{true, true, false, 0x70 /* VK_F1 */, "Ctrl+Alt+F1"};
    RecordResult(!gs::IsSafePanicHotkey(bare) && !gs::IsSafePanicHotkey(single) && !gs::IsSafePanicHotkey(odd_key),
                 "Test 85.6: a bare key, one modifier or an unsupported key is refused");
    gs::PanicHotkey two{true, false, true, '7', "Ctrl+Mayús+7"};
    RecordResult(gs::IsSafePanicHotkey(two), "Test 85.7: two modifiers and a digit are accepted");
}

void Test86() {
    FakeFileSystem fs;
    const std::wstring mark = L"C:\\ProgramData\\VegaFlow\\welcome_seen";
    RecordResult(gs::ShouldShowWelcome(fs, mark), "Test 86.1: the welcome shows while there is no mark");
    RecordResult(gs::MarkWelcomeSeen(fs, mark) && !gs::ShouldShowWelcome(fs, mark), "Test 86.2: after the mark is written the welcome does not show again");
    FakeFileSystem blocked;
    blocked.fail_write = true;
    RecordResult(!gs::MarkWelcomeSeen(blocked, mark) && gs::ShouldShowWelcome(blocked, mark), "Test 86.3: a mark that cannot be written is reported and the card stays");
    FakeFileSystem unreadable;
    unreadable.unreadable.insert(mark); // exists, cannot be read: Exists answers true
    RecordResult(!gs::ShouldShowWelcome(unreadable, mark), "Test 86.4: a mark that exists but cannot be read counts as seen");
}

void Test87() {
    gs::PadNavigator pad;
    RecordResult(pad.Step(0) == gs::PadAction::None, "Test 87.1: no button, no action");
    RecordResult(pad.Step(gs::kPadDpadDown) == gs::PadAction::FocusNext, "Test 87.2: d-pad down moves focus to the next control");
    RecordResult(pad.Step(gs::kPadDpadDown) == gs::PadAction::None, "Test 87.3: a held button acts once");
    pad.Step(0);
    RecordResult(pad.Step(gs::kPadDpadUp) == gs::PadAction::FocusPrev, "Test 87.4: release and press again acts again (d-pad up goes back)");
    pad.Step(0);
    RecordResult(pad.Step(gs::kPadA) == gs::PadAction::Press, "Test 87.5: A presses the focused button");
    pad.Step(0);
    RecordResult(pad.Step(gs::kPadRightShoulder) == gs::PadAction::TabNext && (pad.Step(0), pad.Step(gs::kPadLeftShoulder)) == gs::PadAction::TabPrev, "Test 87.6: the shoulders switch screen");
    pad.Step(0);
    RecordResult(pad.Step(0x4000 | 0x0010) == gs::PadAction::None, "Test 87.7: buttons with no meaning (X, Start) do nothing");
    pad.Step(gs::kPadA);
    RecordResult(pad.Step(gs::kPadA | gs::kPadDpadDown) == gs::PadAction::FocusNext, "Test 87.8: a new press while another is held still acts");
}

void Test88() {
    const vegaflow::SettingSpec* mpo = vegaflow::FindSetting("mpo_disabled");
    RecordResult(mpo != nullptr, "Test 88.0: the MPO row exists in the catalog");
    if (!mpo) return;
    const std::string off = vegaflow::SettingPreview(*mpo, false, true);
    RecordResult(Has(off, "Ahora: apagado") && Has(off, "Si lo enciendes") && Has(off, mpo->target) && Has(off, mpo->change) && Has(off, "Historial"),
                 "Test 88.1: an off row previews the exact write and where to undo it", off);
    const std::string on = vegaflow::SettingPreview(*mpo, true, true);
    RecordResult(Has(on, "Ahora: encendido") && Has(on, "Si lo apagas") && !Has(on, "escribe"), "Test 88.2: an on row previews the way back", on);
    RecordResult(!Has(off, "administrador") && Has(vegaflow::SettingPreview(*mpo, false, false), "no corre como administrador"),
                 "Test 88.3: the administrator warning appears only when VegaFlow is not elevated");
    vegaflow::SettingSpec once;
    once.id = "x";
    once.undoable = false;
    RecordResult(Has(vegaflow::SettingPreview(once, false, true), "no deja un valor original guardado"), "Test 88.4: a one-shot row says it cannot be undone");
    vegaflow::SettingSpec self;
    self.user_switch = false;
    RecordResult(Has(vegaflow::SettingPreview(self, true, true), "lo decide solo"), "Test 88.5: a row without a switch says VegaFlow decides it");

    gs::HomeInputs in;
    in.game_active = true;
    in.game_name = "Octopath";
    in.game_app_id = 921570;
    in.automatic = true;
    const auto m = gs::BuildHomeScreen(in, {}, {});
    RecordResult(m.can_pause && m.pause_app_id == 921570, "Test 88.6: a known running game offers «Pausar VegaFlow en este juego»");
    in.game_app_id = 0;
    RecordResult(!gs::BuildHomeScreen(in, {}, {}).can_pause, "Test 88.7: a game with no known app id offers no pause");
    in.game_app_id = 921570;
    in.automatic = false;
    RecordResult(!gs::BuildHomeScreen(in, {}, {}).can_pause, "Test 88.8: safe mode (VegaFlow acts on nothing) offers no pause");
    in.automatic = true;
    in.game_active = false;
    RecordResult(!gs::BuildHomeScreen(in, {}, {}).can_pause, "Test 88.9: no game open, no pause");
}

} // namespace vftest
