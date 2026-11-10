// Adversarial tests for vegaflow::UniqueResource<Traits> (src/windows/unique_handle.hpp).
// Contract: every valid handle is closed exactly once; an invalid one is never closed; moving transfers ownership;
// release() hands it over without closing; reset(h) closes the previous one and takes h; put() closes the current one
// and returns its address. Most cases use a counting Traits over `int` so double closes are measured without touching
// Windows; a few use real events to check the UniqueHandle specifics (NULL and INVALID_HANDLE_VALUE mean "no handle").

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <map>
#include <string>
#include <utility>

#include "windows/unique_handle.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

// How many times each value was closed. 0 is "empty"; negative values are invalid (like INVALID_HANDLE_VALUE).
std::map<int, int> g_close_count;

struct CountingTraits {
    using value_type = int;
    static int Empty() { return 0; }
    static bool IsValid(int h) { return h > 0; }
    static void Close(int h) { ++g_close_count[h]; }
};

using Counted = vegaflow::UniqueResource<CountingTraits>;

int Closes(int h) {
    const auto it = g_close_count.find(h);
    return it == g_close_count.end() ? 0 : it->second;
}

bool HandleIsOpenAttack(HANDLE h) {
    DWORD flags = 0;
    return GetHandleInformation(h, &flags) != FALSE;
}

// Self move-assignment through a reference, so GCC's -Wself-move does not complain.
void MoveAssign(Counted& to, Counted& from) { to = std::move(from); }

} // namespace

void Test38() {
    // -------------------------------------------------------------------------
    // Test 38: UniqueResource edge cases (attack tests)
    // -------------------------------------------------------------------------
    g_close_count.clear();
    auto pass = [](bool b) { return std::string(b ? "PASS" : "FAIL"); };

    // 1. Self move-assignment: keeps the value, closes nothing now, closes it once at scope end.
    bool self_move = false;
    {
        Counted a(101);
        MoveAssign(a, a);
        self_move = a.get() == 101 && static_cast<bool>(a) && Closes(101) == 0;
    }
    self_move = self_move && Closes(101) == 1;

    // 2. reset() with the value it already owns: must end with exactly one close after scope end.
    {
        Counted a(102);
        a.reset(a.get());
    }
    const bool reset_same = Closes(102) == 1;

    // 3. put() on an owned handle: closes the old one once; the value written through put() is owned and closed once.
    bool put_owned = false;
    {
        Counted a(103);
        int* out = a.put();
        put_owned = Closes(103) == 1 && out != nullptr && *out == 0 && !a;
        *out = 104;
        put_owned = put_owned && a.get() == 104 && static_cast<bool>(a);
    }
    put_owned = put_owned && Closes(103) == 1 && Closes(104) == 1;

    // 4. put() with an invalid sentinel written through it (like an API returning INVALID_HANDLE_VALUE): never closed,
    //    not even after moving it around.
    bool put_invalid = false;
    {
        Counted a;
        *a.put() = -1;
        put_invalid = !a;
        Counted b(std::move(a));
        put_invalid = put_invalid && !b;
        b.reset();
    }
    put_invalid = put_invalid && Closes(-1) == 0;

    // 5. release() twice: first returns the value, second returns empty; nothing closed, ever.
    bool release_twice = false;
    {
        Counted a(105);
        const int first = a.release();
        const int second = a.release();
        release_twice = first == 105 && second == 0 && !a;
    }
    release_twice = release_twice && Closes(105) == 0;

    // 6. Move from an empty object (construction and assignment): nothing closed, both empty.
    bool move_empty = false;
    {
        Counted empty;
        Counted b(std::move(empty));
        Counted c(106);
        c = std::move(empty);  // closes 106 once, then c is empty
        move_empty = !empty && !b && !c && Closes(106) == 1;
    }
    move_empty = move_empty && Closes(106) == 1 && Closes(0) == 0;

    // 7. Destroying a moved-from object: the value is closed once, by the new owner only.
    bool moved_from_destroyed = false;
    {
        Counted owner;
        {
            Counted a(107);
            owner = std::move(a);
        }
        moved_from_destroyed = Closes(107) == 0 && owner.get() == 107;
    }
    moved_from_destroyed = moved_from_destroyed && Closes(107) == 1;

    // 8. Construct with invalid values (0 and -1): empty, never closed; reset to invalid also gives empty.
    bool construct_invalid = false;
    {
        Counted zero(0);
        Counted negative(-1);
        Counted c(108);
        c.reset(-1);
        construct_invalid = !zero && !negative && negative.get() == 0 && !c && c.get() == 0 && Closes(108) == 1;
    }
    construct_invalid = construct_invalid && Closes(0) == 0 && Closes(-1) == 0 && Closes(108) == 1;

    // 9. Move-assign onto an owner: the target's old value is closed once, the source's once at the end.
    bool move_assign_owned = false;
    {
        Counted a(109);
        Counted b(110);
        a = std::move(b);
        move_assign_owned = Closes(109) == 1 && Closes(110) == 0 && a.get() == 110 && !b;
    }
    move_assign_owned = move_assign_owned && Closes(109) == 1 && Closes(110) == 1;

    // 10. std::swap: each value still closed exactly once, by its new owner.
    bool swap_ok = false;
    {
        Counted a(111);
        Counted b(112);
        std::swap(a, b);
        swap_ok = a.get() == 112 && b.get() == 111 && Closes(111) == 0 && Closes(112) == 0;
    }
    swap_ok = swap_ok && Closes(111) == 1 && Closes(112) == 1;

    // Global invariant: no value closed twice, no invalid value closed. Excludes value 102 (case 2) on purpose so a
    // failure there is not counted twice.
    bool no_double_close = true;
    for (const auto& [value, count] : g_close_count) {
        if (value == 102) continue;
        if (value <= 0 || count != 1) no_double_close = false;
    }

    // --- Real handles (UniqueHandle) ---------------------------------------------------------------------------------

    // 11. reset(get()) on a real event: the wrapper must not keep claiming a handle that is already closed.
    //     The handle is released right after measuring, so a second CloseHandle never runs (it could raise under a
    //     debugger or strict handle checks and stop the suite).
    bool real_reset_same = false;
    {
        vegaflow::UniqueHandle e(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        const HANDLE raw = e.get();
        if (raw != nullptr) {
            e.reset(e.get());
            const bool claims_it = static_cast<bool>(e) && e.get() == raw;
            const bool still_open = HandleIsOpenAttack(raw);
            real_reset_same = !claims_it || still_open;  // owning a closed handle is the failure
            (void)e.release();
            if (still_open) CloseHandle(raw);
        }
    }

    // 12. put() on an owned real event closes it; INVALID_HANDLE_VALUE written through put() counts as "no handle".
    bool real_put = false;
    {
        vegaflow::UniqueHandle e(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        const HANDLE raw = e.get();
        HANDLE* out = e.put();
        const bool closed = raw != nullptr && !HandleIsOpenAttack(raw);  // checked before any new handle is created
        *out = INVALID_HANDLE_VALUE;
        real_put = closed && !e;
        e.release();  // INVALID_HANDLE_VALUE must not reach CloseHandle either way
    }

    // 13. Constructing UniqueHandle with NULL or INVALID_HANDLE_VALUE: empty, get() normalised to NULL.
    const vegaflow::UniqueHandle from_null(nullptr);
    const vegaflow::UniqueHandle from_invalid(INVALID_HANDLE_VALUE);
    const bool real_invalid = !from_null && !from_invalid && from_null.get() == nullptr && from_invalid.get() == nullptr;

    // 14. Move-constructing a real event: the moved-from wrapper's destruction does not close it; the new owner does.
    bool real_moved_from = false;
    {
        HANDLE raw = nullptr;
        vegaflow::UniqueHandle owner;
        {
            vegaflow::UniqueHandle a(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            raw = a.get();
            owner = std::move(a);
        }
        real_moved_from = raw != nullptr && HandleIsOpenAttack(raw) && owner.get() == raw;
        owner.reset();
        real_moved_from = real_moved_from && !HandleIsOpenAttack(raw);
    }

    const bool ok = self_move && reset_same && put_owned && put_invalid && release_twice && move_empty &&
                    moved_from_destroyed && construct_invalid && move_assign_owned && swap_ok && no_double_close &&
                    real_reset_same && real_put && real_invalid && real_moved_from;
    RecordResult(ok, "Test 38: UniqueResource edge cases",
                 "self move: " + pass(self_move) + ", reset(get()): " + pass(reset_same) + " (closes of 102: " +
                     std::to_string(Closes(102)) + ")" + ", put on owned: " + pass(put_owned) +
                     ", put invalid sentinel: " + pass(put_invalid) + ", release twice: " + pass(release_twice) +
                     ", move from empty: " + pass(move_empty) + ", moved-from destroyed: " +
                     pass(moved_from_destroyed) + ", construct invalid: " + pass(construct_invalid) +
                     ", move-assign owned: " + pass(move_assign_owned) + ", swap: " + pass(swap_ok) +
                     ", no double close: " + pass(no_double_close) + ", real reset(get()): " + pass(real_reset_same) +
                     ", real put: " + pass(real_put) + ", real invalid: " + pass(real_invalid) +
                     ", real moved-from: " + pass(real_moved_from));
}

} // namespace vftest
