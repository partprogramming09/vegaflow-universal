#pragma once
// F2.7: the plain sentences that say why a hardware profile is what it is. The words live in Rust; this is the bridge call.

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow::session {

// One sentence per bit of ApuOptimizationPolicy::decision_reasons, in bit order; bits with no rule are skipped.
std::vector<std::string> DecisionReasonTexts(uint64_t reasons);

} // namespace vegaflow::session
