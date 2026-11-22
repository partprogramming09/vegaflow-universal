#pragma once
// Host-build stand-in for <windows.h>: only the types and constants the headers under src/ name in declarations.
// No Windows function is declared, so code that really calls Windows fails to compile instead of running a fake.
// Used by scripts/ci/host-unit-tests.sh.
#include <cstdint>

typedef uint32_t ULONG; // 32-bit on Windows, unlike the host's unsigned long
typedef ULONG* PULONG;
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef void* PVOID;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HKEY;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef uintptr_t ULONG_PTR;

// MSVC CRT functions that src/ uses; real implementations over POSIX, not fakes.
#include <ctime>
#include <cwchar>
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int localtime_s(std::tm* out, const std::time_t* t) { return localtime_r(t, out) ? 0 : 1; }

// Handle and message types that src/ui/gui_components.hpp names (the contrast test only needs its color constants).
typedef void* HBRUSH;
typedef void* HDC;
typedef void* HFONT;
typedef uintptr_t UINT_PTR;
struct RECT { LONG left, top, right, bottom; };
#define WM_USER 0x0400

// GDI colors are plain integers (0x00BBGGRR); these are the real definitions.
typedef DWORD COLORREF;
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))

#define NTSYSAPI
#define NTAPI
#define NORMAL_PRIORITY_CLASS 0x00000020
