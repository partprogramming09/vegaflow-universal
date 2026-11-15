#pragma once
// The keyboard shortcut that puts everything back ("Algo va mal", no <windows.h>). It needs at least two modifiers so a
// stray key press or a game's own shortcut cannot trigger it.

#include <string>

namespace vegaflow::session {

struct PanicHotkey {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    unsigned vk = 0;   // virtual-key code: 'A'..'Z', '0'..'9'
    std::string label; // what the person reads, e.g. "Ctrl+Alt+Mayús+R"
};

PanicHotkey DefaultPanicHotkey(); // Ctrl+Alt+Shift+R (R of "revertir")
bool IsSafePanicHotkey(const PanicHotkey& h);

} // namespace vegaflow::session
