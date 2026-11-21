#pragma once
// The first-run welcome card of Inicio: shown until the person closes it once. The "seen" mark is a small file next to
// config.json, so it needs no new setting in the Rust core (no <windows.h>).

#include <string>

#include "ports/file_system.hpp"

namespace vegaflow::session {

// True while the mark does not exist. When the file system cannot tell (Exists answers true on any error) the card is
// not shown: a card that cannot be dismissed would nag forever.
bool ShouldShowWelcome(FileSystem& fs, const std::wstring& mark_path);
// False when the mark could not be written (the card will show again next time).
bool MarkWelcomeSeen(FileSystem& fs, const std::wstring& mark_path);

} // namespace vegaflow::session
