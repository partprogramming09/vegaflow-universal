#pragma once
// Owning wrappers for Windows handles: the handle is closed when the wrapper goes out of scope, also on early
// returns and error paths. Move-only. (WIL does this too, but it does not support MinGW.)
//
//   UniqueHandle  CloseHandle  (processes, threads, events, snapshots, files). NULL and INVALID_HANDLE_VALUE both
//                              count as "no handle": APIs return one or the other on failure. Test with
//                              operator bool, not get(): a value written through put() is kept as the API left it.
//   UniqueRegKey  RegCloseKey  (keys from RegOpenKeyExW / RegCreateKeyExW). Never wrap HKEY_LOCAL_MACHINE and the
//                              other predefined roots.
//   UniqueFindHandle FindClose (FindFirstFileW).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <utility>

namespace vegaflow {

template <typename Traits>
class UniqueResource {
public:
    using value_type = typename Traits::value_type;

    UniqueResource() = default;
    explicit UniqueResource(value_type h) : h_(Traits::IsValid(h) ? h : Traits::Empty()) {}
    ~UniqueResource() { reset(); }

    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;
    UniqueResource(UniqueResource&& other) noexcept : h_(other.release()) {}
    UniqueResource& operator=(UniqueResource&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }

    value_type get() const { return h_; }
    explicit operator bool() const { return Traits::IsValid(h_); }

    // Gives up ownership without closing.
    value_type release() {
        value_type h = h_;
        h_ = Traits::Empty();
        return h;
    }

    // Closes the current handle (if any) and takes `h`. Resetting to the handle already owned keeps it open:
    // closing it and then holding it would close it a second time later.
    void reset(value_type h = Traits::Empty()) {
        if (h == h_) return;
        if (Traits::IsValid(h_)) Traits::Close(h_);
        h_ = Traits::IsValid(h) ? h : Traits::Empty();
    }

    // For APIs that return the handle through an out-parameter: closes the current one and returns its address.
    value_type* put() {
        reset();
        return &h_;
    }

private:
    value_type h_ = Traits::Empty();
};

struct HandleTraits {
    using value_type = HANDLE;
    static HANDLE Empty() { return nullptr; }
    static bool IsValid(HANDLE h) { return h != nullptr && h != INVALID_HANDLE_VALUE; }
    static void Close(HANDLE h) { CloseHandle(h); }
};

struct RegKeyTraits {
    using value_type = HKEY;
    static HKEY Empty() { return nullptr; }
    static bool IsValid(HKEY k) { return k != nullptr; }
    static void Close(HKEY k) { RegCloseKey(k); }
};

struct FindHandleTraits {
    using value_type = HANDLE;
    static HANDLE Empty() { return INVALID_HANDLE_VALUE; }
    static bool IsValid(HANDLE h) { return h != nullptr && h != INVALID_HANDLE_VALUE; }
    static void Close(HANDLE h) { FindClose(h); }
};

using UniqueHandle = UniqueResource<HandleTraits>;
using UniqueRegKey = UniqueResource<RegKeyTraits>;
using UniqueFindHandle = UniqueResource<FindHandleTraits>;

} // namespace vegaflow
