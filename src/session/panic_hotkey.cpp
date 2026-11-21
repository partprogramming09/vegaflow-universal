#include "session/panic_hotkey.hpp"

namespace vegaflow::session {

PanicHotkey DefaultPanicHotkey() { return {true, true, true, 'R', "Ctrl+Alt+Mayús+R"}; }

bool IsSafePanicHotkey(const PanicHotkey& h) {
    const int modifiers = (h.ctrl ? 1 : 0) + (h.alt ? 1 : 0) + (h.shift ? 1 : 0);
    const bool key_ok = (h.vk >= 'A' && h.vk <= 'Z') || (h.vk >= '0' && h.vk <= '9');
    return modifiers >= 2 && key_ok;
}

} // namespace vegaflow::session
