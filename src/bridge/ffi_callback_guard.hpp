#pragma once
// C++ callbacks that Rust calls must never throw: an exception unwinding through an extern "C" frame is undefined
// behaviour. The callback body runs inside RunCallbackGuarded; the caller checks ConsumeCallbackFailure() after the
// Rust call and treats true as "the data is incomplete" (refuse to write, never use a partial result).

namespace vegaflow {

inline thread_local bool g_ffi_callback_threw = false;

template <typename F>
void RunCallbackGuarded(F&& body) noexcept {
    try {
        body();
    } catch (...) {
        g_ffi_callback_threw = true;
    }
}

// Returns whether any guarded callback threw since the last call, and clears it.
inline bool ConsumeCallbackFailure() noexcept {
    const bool threw = g_ffi_callback_threw;
    g_ffi_callback_threw = false;
    return threw;
}

} // namespace vegaflow
