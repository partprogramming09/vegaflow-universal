#pragma once
// Model of the "Requisitos y advertencias" screen: what VegaFlow needs, whether this PC meets it (from what the probe
// read) and what is worth knowing. A reading that failed is "No se pudo leer", never a pass.

#include <string>
#include <vector>

#include "session/system_info.hpp"

namespace vegaflow::session {

enum class RequirementState { Meets, Missing, Limited, Unknown, Optional };

struct RequirementRow {
    std::string id; // windows, permissions, processor, graphics, steam
    std::string what;
    std::string detail;
    RequirementState state = RequirementState::Unknown;
};

struct RequirementsScreenModel {
    std::vector<RequirementRow> rows;
    std::vector<std::string> warnings;
};

const char* StateLabel(RequirementState state);

RequirementsScreenModel BuildRequirementsScreen(const SystemInfoSnapshot& snapshot, bool elevated, bool steam_found); // steam_found: the library scan found at least one game

} // namespace vegaflow::session
