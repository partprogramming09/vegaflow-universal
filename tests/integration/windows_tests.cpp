// Integration tests: the RAII wrappers in src/windows/unique_handle.hpp, against real kernel objects and registry keys.
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <utility>

#include "windows/unique_handle.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

bool HandleIsOpen(HANDLE h) {
    DWORD flags = 0;
    return GetHandleInformation(h, &flags) != FALSE;
}

bool KeyIsOpen(HKEY k) {
    DWORD subkeys = 0;
    return RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                            nullptr) == ERROR_SUCCESS;
}

} // namespace

void Test35() {
    // -------------------------------------------------------------------------
    // Test 35: UniqueHandle and UniqueRegKey close what they own exactly once
    // -------------------------------------------------------------------------
    HANDLE raw = nullptr;
    {
        vegaflow::UniqueHandle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        raw = event.get();
    }
    const bool closed_at_scope_end = raw != nullptr && !HandleIsOpen(raw);

    vegaflow::UniqueHandle first(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    const HANDLE moved_raw = first.get();
    vegaflow::UniqueHandle second(std::move(first));
    const bool move_keeps_it_open = !first && second.get() == moved_raw && HandleIsOpen(moved_raw);
    second.reset();
    const bool reset_closes = !second && !HandleIsOpen(moved_raw);

    vegaflow::UniqueHandle kept(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    const HANDLE released = kept.release();
    const bool release_does_not_close = !kept && HandleIsOpen(released);
    CloseHandle(released);

    const vegaflow::UniqueHandle invalid(INVALID_HANDLE_VALUE);
    const bool invalid_is_empty = !invalid;

    HKEY raw_key = nullptr;
    {
        vegaflow::UniqueRegKey key;
        const bool opened = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, key.put()) == ERROR_SUCCESS;
        raw_key = key.get();
        if (!opened || !KeyIsOpen(raw_key)) raw_key = nullptr;
    }
    const bool key_closed = raw_key != nullptr && !KeyIsOpen(raw_key);

    const bool ok = closed_at_scope_end && move_keeps_it_open && reset_closes && release_does_not_close && invalid_is_empty &&
                    key_closed;
    auto pass = [](bool b) { return std::string(b ? "PASS" : "FAIL"); };
    RecordResult(ok, "Test 35: UniqueHandle and UniqueRegKey close what they own exactly once",
                 "scope end: " + pass(closed_at_scope_end) + ", move: " + pass(move_keeps_it_open) + ", reset: " +
                     pass(reset_closes) + ", release: " + pass(release_does_not_close) + ", invalid: " +
                     pass(invalid_is_empty) + ", registry key: " + pass(key_closed));
}

} // namespace vftest
