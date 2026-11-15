#include "session/pad_nav.hpp"

namespace vegaflow::session {

PadAction PadNavigator::Step(uint16_t buttons) {
    const uint16_t pressed = static_cast<uint16_t>(buttons & ~previous_);
    previous_ = buttons;
    if (pressed & (kPadDpadDown | kPadDpadRight)) return PadAction::FocusNext;
    if (pressed & (kPadDpadUp | kPadDpadLeft)) return PadAction::FocusPrev;
    if (pressed & kPadA) return PadAction::Press;
    if (pressed & kPadRightShoulder) return PadAction::TabNext;
    if (pressed & kPadLeftShoulder) return PadAction::TabPrev;
    return PadAction::None;
}

} // namespace vegaflow::session
