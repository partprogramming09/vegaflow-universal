#pragma once
// E2.7: what the window does with the phase G extras, as flows without <windows.h> so tests can drive them: closing with
// changes in force, the panic shortcut, game-pad polling, the restart notice and the diagnosis panel. The window only
// forwards its messages to these and draws what they answer.

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "session/config_screen.hpp"
#include "session/exit_notice.hpp"
#include "session/pad_nav.hpp"
#include "session/panic_hotkey.hpp"

namespace vegaflow::session {

enum class CloseStep { Close, ShowDialog, Ignore };
enum class CloseChoice { RestoreAndClose, KeepAndClose, Cancel };

// WM_CLOSE: ask only when changes are in force, and never open a second dialog while one is open.
class CloseFlow {
public:
    // `question` is filled on ShowDialog. Ignore: a dialog is already open (a second WM_CLOSE).
    CloseStep OnClose(const HistoryScreenModel& history, std::string* question);
    // True when the window must close. RestoreAndClose runs `reset_all` first; Cancel reopens the way for the next close.
    bool OnChoice(CloseChoice choice, const std::function<void()>& reset_all);
    bool dialog_open() const { return open_; }

private:
    bool open_ = false;
};

// RegisterHotKey modifier bits (MOD_*), repeated here to keep this file free of <windows.h>.
inline constexpr unsigned kHotModAlt = 0x1, kHotModControl = 0x2, kHotModShift = 0x4, kHotModNoRepeat = 0x4000;

struct PanicHotkeyPlan {
    unsigned modifiers = 0;
    unsigned vk = 0; // 0: do not register (the shortcut is not safe)
};
PanicHotkeyPlan PlanPanicHotkey(const PanicHotkey& h);
// "" when registered; otherwise the sentence Configuración shows (another program owns the shortcut).
std::string PanicHotkeyNotice(const PanicHotkey& h, bool registered);

// One restore at a time. A press that waits in the message queue while the restore runs arrives after it ended, so a
// press within kPanicCooldown of the end is the same press.
class PanicGuard {
public:
    static constexpr std::chrono::milliseconds kPanicCooldown{1500};
    bool TryBegin(std::chrono::steady_clock::time_point now);
    void End(std::chrono::steady_clock::time_point now);

private:
    bool running_ = false;
    bool has_ended_ = false;
    std::chrono::steady_clock::time_point ended_{};
};

// `read` fills the buttons of the first pad; null or false (no xinput1_4.dll, no pad) is "no input".
PadAction PollPad(PadNavigator& nav, const std::function<bool(uint16_t*)>& read);

// "Se aplica al reiniciar VegaFlow" for a Configuración row read only at start, "" otherwise.
std::string RestartNotice(const ConfigRow& row);

// The diagnosis text, or a sentence when nothing could be read.
std::string DoctorPanelText(const std::string& report);

} // namespace vegaflow::session
