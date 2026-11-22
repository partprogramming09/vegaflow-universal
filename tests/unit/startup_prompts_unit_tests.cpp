// Unit tests: the two startup questions (restore point, revert pending changes) only reach a person on a visible
// console, and "nobody answered" (hidden console, pipe, file, timeout, end of input) always takes the safe default.

#include <optional>
#include <string>

#include "session/startup_prompts.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test136() {
    namespace s = vegaflow::session;
    using K = vegaflow::StdinKind;

    // Only a visible console has someone who can type; a hidden console (task, Start-Process -WindowStyle Hidden),
    // a pipe, a file or no stdin at all never answers.
    const bool who = s::CanAskPerson(K::VisibleConsole) && !s::CanAskPerson(K::HiddenConsole) &&
                     !s::CanAskPerson(K::Pipe) && !s::CanAskPerson(K::File) && !s::CanAskPerson(K::None);

    // Revert pending changes [S/n]: no answer never reverts; an answered empty line keeps today's "yes".
    const bool revert = !s::ShouldRevertPending(std::nullopt) && s::ShouldRevertPending(std::string("")) &&
                        s::ShouldRevertPending(std::string(" S ")) && s::ShouldRevertPending(std::string("sí")) &&
                        !s::ShouldRevertPending(std::string("n")) && !s::ShouldRevertPending(std::string("no"));

    // Restore point [s/N]: no answer means "not asked" (nullopt, the journal stays unmarked); empty or "n" is a real no.
    const bool rp = !s::RestorePointAnswer(std::nullopt).has_value() &&
                    s::RestorePointAnswer(std::string("")) == std::optional<bool>(false) &&
                    s::RestorePointAnswer(std::string("n")) == std::optional<bool>(false) &&
                    s::RestorePointAnswer(std::string("yes")) == std::optional<bool>(true) &&
                    s::RestorePointAnswer(std::string("Si")) == std::optional<bool>(true);

    RecordResult(who && revert && rp, "Test 136: startup questions ask only a visible console; no answer is the safe default",
                 std::string("Who=") + (who ? "1" : "0") + ", Revert=" + (revert ? "1" : "0") + ", RestorePoint=" + (rp ? "1" : "0"));
}

} // namespace vftest
