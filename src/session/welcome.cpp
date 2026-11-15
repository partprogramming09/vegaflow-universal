#include "session/welcome.hpp"

namespace vegaflow::session {

bool ShouldShowWelcome(FileSystem& fs, const std::wstring& mark_path) { return !fs.Exists(mark_path); }

bool MarkWelcomeSeen(FileSystem& fs, const std::wstring& mark_path) { return fs.Write(mark_path, "seen\n"); }

} // namespace vegaflow::session
