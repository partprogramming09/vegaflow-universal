#include "session/profile_reasons.hpp"

#include "bridge/rust_bridge.h"

namespace vegaflow::session {

namespace {

void CollectReasonLine(void* ctx, const char* utf8, size_t len) noexcept {
    try {
        static_cast<std::vector<std::string>*>(ctx)->emplace_back(utf8, len);
    } catch (...) {
        // out of memory: the line is lost, the others still arrive
    }
}

} // namespace

std::vector<std::string> DecisionReasonTexts(uint64_t reasons) {
    std::vector<std::string> lines;
    vf_rust_decision_reason_texts(reasons, CollectReasonLine, &lines);
    return lines;
}

} // namespace vegaflow::session
