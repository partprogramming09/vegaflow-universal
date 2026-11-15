#pragma once
// Game-pad navigation of the VegaFlow window: which XInput buttons mean what, and that a held button acts once (no <windows.h>).

#include <cstdint>

namespace vegaflow::session {

enum class PadAction { None, FocusNext, FocusPrev, Press, TabNext, TabPrev };

// XInput button bits (XINPUT_GAMEPAD_*).
inline constexpr uint16_t kPadDpadUp = 0x0001, kPadDpadDown = 0x0002, kPadDpadLeft = 0x0004, kPadDpadRight = 0x0008;
inline constexpr uint16_t kPadLeftShoulder = 0x0100, kPadRightShoulder = 0x0200, kPadA = 0x1000;

class PadNavigator {
public:
    // The state of the buttons now; the action of the button that was just pressed (rising edge). One action per call.
    PadAction Step(uint16_t buttons);
    void Reset() { previous_ = 0; }

private:
    uint16_t previous_ = 0;
};

} // namespace vegaflow::session
