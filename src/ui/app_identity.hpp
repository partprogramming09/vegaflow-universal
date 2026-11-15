#pragma once

// What VegaFlow calls itself in the window, the tray and `--version`, and the one-line hardware summary. Names only,
// no Windows calls, so the daemon entry point and the window share one definition and tests can pin it.

#include <string>

namespace vegaflow::ui {

// Must match rust/vegaflow-core/Cargo.toml; scripts/package.sh refuses to build a package if they differ.
inline constexpr char kVersion[] = "0.1.0";

inline constexpr wchar_t kWindowClass[] = L"VegaFlowUniversalWindow";
inline constexpr wchar_t kWindowTitle[] = L"VegaFlow Universal";
inline constexpr wchar_t kTrayTip[] = L"VegaFlow Universal";

// WM_COMMAND id of the tray menu entry "Restaurar" (sent by the menu, inside our own process).
inline constexpr int kTrayRestoreCommand = 3001;

// Registered message a second launch posts to the running window to bring it forward. It is the only message let through
// the UIPI filter: WM_COMMAND is not, so a lower-integrity process cannot click the elevated window's buttons.
inline constexpr wchar_t kRestoreWindowMessage[] = L"VegaFlowUniversal.RestoreWindow";

// "CPU  |  GPU" from what this machine reported. Empty parts say so instead of borrowing another machine's name;
// a line over 65 bytes is cut with "..." on a character boundary, never replaced.
inline std::string FormatHardwareLine(const std::string& cpu, const std::string& gpu) {
    constexpr size_t kMax = 65;
    const std::string line = (cpu.empty() ? std::string("Procesador no identificado") : cpu) + "  |  " +
                             (gpu.empty() ? std::string("Gráfica no identificada") : gpu);
    if (line.size() <= kMax) return line;
    size_t cut = kMax - 3;
    while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut; // never split a UTF-8 sequence
    return line.substr(0, cut) + "...";
}

} // namespace vegaflow::ui
