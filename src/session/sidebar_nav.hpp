#pragma once
// Keyboard logic of the sidebar: which entry is active for a tab, where the arrows go, which tab Ctrl+digit opens.

namespace vegaflow::session {

inline constexpr int kSidebarEntries = 8; // Requisitos (tab 8) hangs from Configuración and is not listed

enum class SidebarKey { Up, Down, Home, End };

// The sidebar entry highlighted for tab `tab`; an unknown tab highlights the first entry.
int SidebarEntryOfTab(int tab);
// Where the focus goes from entry `index` (wraps at both ends; a stale index restarts at the first entry).
int MoveSidebarFocus(int index, SidebarKey key);
// '1'..'8' to tab 0..7, -1 for anything else.
int TabOfDigit(int digit_char);

} // namespace vegaflow::session
