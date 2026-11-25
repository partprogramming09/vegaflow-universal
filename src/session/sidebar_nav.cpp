#include "session/sidebar_nav.hpp"

namespace vegaflow::session {

int SidebarEntryOfTab(int tab) {
    if (tab == 8) return 5;
    return tab >= 0 && tab < kSidebarEntries ? tab : 0;
}

int MoveSidebarFocus(int index, SidebarKey key) {
    if (index < 0 || index >= kSidebarEntries) return 0;
    switch (key) {
    case SidebarKey::Up: return (index + kSidebarEntries - 1) % kSidebarEntries;
    case SidebarKey::Down: return (index + 1) % kSidebarEntries;
    case SidebarKey::Home: return 0;
    case SidebarKey::End: return kSidebarEntries - 1;
    }
    return index;
}

int TabOfDigit(int c) { return c >= '1' && c < '1' + kSidebarEntries ? c - '1' : -1; }

} // namespace vegaflow::session
