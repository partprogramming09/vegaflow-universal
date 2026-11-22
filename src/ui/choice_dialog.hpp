#pragma once
// E2.7: the window's own modal dialog (never MessageBox): a title, a read-only text that scrolls and a row of buttons.
// Used by the exit notice, the welcome and the diagnosis.

#include <functional>
#include <string>
#include <vector>

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

struct DialogButton {
    std::wstring label;
    int result = 0;
    bool closes = true;           // false: the button acts (`on_click`) and the dialog stays open
    std::function<void()> on_click; // runs before the dialog closes (or instead of closing)
};

// Blocks until a closing button is pressed or the dialog is closed with its X / Esc (answer `cancel_result`).
// `monospace` shows the text in a fixed font (the diagnosis).
int ShowChoiceDialog(HWND owner, const std::wstring& title, const std::wstring& body, const std::vector<DialogButton>& buttons, int cancel_result,
                     bool monospace = false);
// True while any dialog of this kind is open (the game pad and the exit flow read it).
bool ChoiceDialogOpen();
// Plain Unicode text on the clipboard; the person's own clipboard is emptied only when there is something to put in it.
bool CopyTextToClipboard(HWND owner, const std::wstring& text);

} // namespace vegaflow::ui
