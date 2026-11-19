// Unit test for E1.5: an exception inside a callback called from Rust is caught, flagged and reported once.

#include "bridge/ffi_callback_guard.hpp"

#include "../test_support.hpp"

#include <new>
#include <stdexcept>
#include <type_traits>

namespace vftest {

namespace {
void ThrowingCallback(void*, const char*, size_t) noexcept {
    vegaflow::RunCallbackGuarded([] { throw std::bad_alloc(); });
}
} // namespace

void Test90() {
    vegaflow::ConsumeCallbackFailure();
    bool survived = true;
    ThrowingCallback(nullptr, "x", 1); // would call std::terminate if the exception escaped a noexcept function
    const bool flagged = vegaflow::ConsumeCallbackFailure();
    const bool cleared = !vegaflow::ConsumeCallbackFailure();
    vegaflow::RunCallbackGuarded([] { throw std::runtime_error("any"); });
    const bool other_exception = vegaflow::ConsumeCallbackFailure();
    vegaflow::RunCallbackGuarded([] {});
    const bool quiet_when_fine = !vegaflow::ConsumeCallbackFailure();
    const bool is_noexcept = noexcept(ThrowingCallback(nullptr, nullptr, 0));

    const bool ok = survived && flagged && cleared && other_exception && quiet_when_fine && is_noexcept;
    RecordResult(ok, "Test 90: FFI callback guard catches exceptions and reports the failure once",
                 std::string("Flagged=") + (flagged ? "OK" : "FAIL") + ", ClearsAfterRead=" + (cleared ? "OK" : "FAIL") +
                     ", AnyException=" + (other_exception ? "OK" : "FAIL") + ", Quiet=" + (quiet_when_fine ? "OK" : "FAIL") +
                     ", Noexcept=" + (is_noexcept ? "OK" : "FAIL"));
}

} // namespace vftest
