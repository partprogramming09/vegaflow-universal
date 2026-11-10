#include "optimize/dxvk_manager.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace vegaflow {

namespace {

bool EndsWithInsensitive(std::wstring_view str, std::wstring_view suffix) {
    if (suffix.size() > str.size()) return false;
    auto tail = str.substr(str.size() - suffix.size());
    return std::equal(
        tail.begin(), tail.end(),
        suffix.begin(), suffix.end(),
        [](wchar_t a, wchar_t b) {
            return std::towlower(a) == std::towlower(b);
        }
    );
}

std::filesystem::path ResolveGameDirectory(const std::wstring& game_exe_path) {
    if (game_exe_path.empty()) return {};
    std::error_code ec;
    std::filesystem::path p(game_exe_path);
    if (std::filesystem::is_directory(p, ec)) {
        return p;
    }
    return p.parent_path();
}

bool CheckIfExeIs64Bit(const std::filesystem::path& exe_path, bool fallback) {
    std::ifstream f(exe_path, std::ios::binary);
    if (!f.is_open()) return fallback;

    IMAGE_DOS_HEADER dos{};
    if (!f.read(reinterpret_cast<char*>(&dos), sizeof(dos))) return fallback;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return fallback;

    f.seekg(dos.e_lfanew, std::ios::beg);
    if (!f) return fallback;

    DWORD nt_sig = 0;
    if (!f.read(reinterpret_cast<char*>(&nt_sig), sizeof(nt_sig))) return fallback;
    if (nt_sig != IMAGE_NT_SIGNATURE) return fallback;

    IMAGE_FILE_HEADER file_header{};
    if (!f.read(reinterpret_cast<char*>(&file_header), sizeof(file_header))) return fallback;

    if (file_header.Machine == IMAGE_FILE_MACHINE_AMD64) {
        return true;
    }
    if (file_header.Machine == IMAGE_FILE_MACHINE_I386) {
        return false;
    }
    return fallback;
}

bool AreFilesIdentical(const std::filesystem::path& p1, const std::filesystem::path& p2) {
    std::error_code ec;
    uintmax_t s1 = std::filesystem::file_size(p1, ec);
    if (ec) return false;
    uintmax_t s2 = std::filesystem::file_size(p2, ec);
    if (ec || s1 != s2) return false;

    std::ifstream f1(p1, std::ios::binary);
    std::ifstream f2(p2, std::ios::binary);
    if (!f1.is_open() || !f2.is_open()) return false;

    constexpr size_t BUF_SIZE = 65536;
    std::vector<char> b1(BUF_SIZE);
    std::vector<char> b2(BUF_SIZE);
    while (f1 && f2) {
        f1.read(b1.data(), BUF_SIZE);
        f2.read(b2.data(), BUF_SIZE);
        std::streamsize c1 = f1.gcount();
        std::streamsize c2 = f2.gcount();
        if (c1 != c2) return false;
        if (c1 == 0) break;
        if (std::memcmp(b1.data(), b2.data(), static_cast<size_t>(c1)) != 0) return false;
    }
    return true;
}

} // namespace

std::wstring DxvkManager::GetDxvkAssetsPath() {
    std::error_code ec;

    // 1. Relative to the CWD
    std::filesystem::path pCwd = std::filesystem::current_path(ec) / L"assets" / L"dxvk" / L"dxvk-2.5.3";
    if (!ec && std::filesystem::exists(pCwd, ec) && std::filesystem::is_directory(pCwd, ec)) {
        return std::filesystem::absolute(pCwd, ec).wstring();
    }

    // 2. Relative to the directory of the VegaFlow executable itself
    wchar_t exePathBuf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::filesystem::path exeDir = std::filesystem::path(exePathBuf).parent_path();

        std::filesystem::path pExe = exeDir / L"assets" / L"dxvk" / L"dxvk-2.5.3";
        if (std::filesystem::exists(pExe, ec) && std::filesystem::is_directory(pExe, ec)) {
            return std::filesystem::absolute(pExe, ec).wstring();
        }

        std::filesystem::path pParent = exeDir.parent_path() / L"assets" / L"dxvk" / L"dxvk-2.5.3";
        if (std::filesystem::exists(pParent, ec) && std::filesystem::is_directory(pParent, ec)) {
            return std::filesystem::absolute(pParent, ec).wstring();
        }

        std::filesystem::path pParent2 = exeDir.parent_path().parent_path() / L"assets" / L"dxvk" / L"dxvk-2.5.3";
        if (std::filesystem::exists(pParent2, ec) && std::filesystem::is_directory(pParent2, ec)) {
            return std::filesystem::absolute(pParent2, ec).wstring();
        }
    }

    return L"";
}

bool DxvkManager::IsDxvkInstalled(const std::wstring& game_exe_path) {
    if (game_exe_path.empty()) return false;

    std::filesystem::path dir = ResolveGameDirectory(game_exe_path);
    if (dir.empty()) return false;

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return false;
    }

    std::filesystem::path d3d11 = dir / L"d3d11.dll";
    std::filesystem::path dxgi = dir / L"dxgi.dll";

    return std::filesystem::exists(d3d11, ec) && std::filesystem::exists(dxgi, ec);
}

bool DxvkManager::DeployDxvk(
    const std::wstring& game_exe_path,
    const ApuOptimizationPolicy& policy,
    bool has_anticheat,
    bool is_64bit,
    bool dry_run,
    std::string* out_message) {

    if (has_anticheat) {
        if (out_message) {
            *out_message = "Bloqueado: El juego utiliza Anti-Cheat (EAC/BattlEye/etc.) y no es seguro inyectar DXVK";
        }
        return false;
    }

    if (dry_run) {
        if (out_message) {
            *out_message = "DXVK Vulkan desplegado con éxito (GPL activo en 6 hilos) [DRY-RUN]";
        }
        return true;
    }

    std::filesystem::path dir = ResolveGameDirectory(game_exe_path);
    if (dir.empty()) {
        if (out_message) {
            *out_message = "Error: Ruta de juego inválida o vacía";
        }
        return false;
    }

    std::error_code ec;
    std::wstring assetsRoot = GetDxvkAssetsPath();
    if (assetsRoot.empty()) {
        if (out_message) {
            *out_message = "Error: No se encontraron los binarios de DXVK en assets/dxvk/dxvk-2.5.3";
        }
        return false;
    }
    fs_.AllowReading(assetsRoot);

    // Detect whether the executable is 64-bit or 32-bit if possible
    std::filesystem::path pExe(game_exe_path);
    if (std::filesystem::exists(pExe, ec) && std::filesystem::is_regular_file(pExe, ec)) {
        is_64bit = CheckIfExeIs64Bit(pExe, is_64bit);
    }

    std::filesystem::path sourceDir = std::filesystem::path(assetsRoot) / (is_64bit ? L"x64" : L"x32");
    std::filesystem::path srcD3d11 = sourceDir / L"d3d11.dll";
    std::filesystem::path srcDxgi = sourceDir / L"dxgi.dll";

    if (!std::filesystem::exists(srcD3d11, ec) || !std::filesystem::exists(srcDxgi, ec)) {
        if (out_message) {
            *out_message = "Error: Binarios de DXVK incompletos en la carpeta de assets (" + (is_64bit ? std::string("x64") : std::string("x32")) + ")";
        }
        return false;
    }

    std::filesystem::path dstD3d11 = dir / L"d3d11.dll";
    std::filesystem::path dstDxgi = dir / L"dxgi.dll";
    std::filesystem::path bakD3d11 = dir / L"d3d11.dll.original.bak";
    std::filesystem::path bakDxgi = dir / L"dxgi.dll.original.bak";

    // 1. Back up d3d11.dll if it existed and is not already the DXVK file
    if (std::filesystem::exists(dstD3d11, ec)) {
        if (!std::filesystem::exists(bakD3d11, ec)) {
            if (!AreFilesIdentical(dstD3d11, srcD3d11)) {
                fs_.Copy(dstD3d11.wstring(), bakD3d11.wstring(), true, has_anticheat);
            }
        }
    }

    // 2. Back up dxgi.dll if it existed and is not already the DXVK file
    if (std::filesystem::exists(dstDxgi, ec)) {
        if (!std::filesystem::exists(bakDxgi, ec)) {
            if (!AreFilesIdentical(dstDxgi, srcDxgi)) {
                fs_.Copy(dstDxgi.wstring(), bakDxgi.wstring(), true, has_anticheat);
            }
        }
    }

    // 3. Copy DXVK DLLs, protected by SandboxGuard
    if (!fs_.Copy(srcD3d11.wstring(), dstD3d11.wstring(), true, has_anticheat)) {
        if (out_message) {
            *out_message = "Error al copiar d3d11.dll a la carpeta del juego (bloqueado por SandboxGuard o fallo de I/O)";
        }
        return false;
    }

    if (!fs_.Copy(srcDxgi.wstring(), dstDxgi.wstring(), true, has_anticheat)) {
        if (out_message) {
            *out_message = "Error al copiar dxgi.dll a la carpeta del juego (bloqueado por SandboxGuard o fallo de I/O)";
        }
        return false;
    }

    // 4. Write dxvk.conf with an APU-optimized profile according to the HardwareProfiler Policy
    std::filesystem::path confPath = dir / L"dxvk.conf";
    {
        std::string confContent =
            "# VegaFlow DXVK AMD Vega 6 Profile (GPL + Async Fallback)\n"
            "dxvk.gpl = True\n"
            "dxvk.enableAsync = True\n"
            "dxvk.gplAsyncCache = True\n"
            "dxvk.numCompilerThreads = " + std::to_string(policy.dxvk_compiler_threads) + "\n"
            "dxvk.maxChunkSize = 256\n"
            "dxvk.enableMemoryChunkSuballocation = True\n"
            "d3d11.samplerAnisotropy = " + std::to_string(policy.max_anisotropy) + "\n"
            "dxgi.syncInterval = 0\n"
            "dxgi.maxDeviceMemory = " + std::to_string(policy.safe_vram_pool_mb) + "\n"
            "dxgi.maxSharedMemory = " + std::to_string(policy.safe_shared_pool_mb) + "\n";
        if (!fs_.Write(confPath.wstring(), confContent, has_anticheat)) {
            if (out_message) {
                *out_message = "Error al escribir dxvk.conf en la carpeta del juego (bloqueado por SandboxGuard o fallo de I/O)";
            }
            return false;
        }
    }

    if (out_message) {
        *out_message = "DXVK-GPLAsync Vulkan desplegado con éxito (GPL + Async en 6 hilos)";
    }
    return true;
}

bool DxvkManager::RemoveDxvk(
    const std::wstring& game_exe_path,
    bool dry_run,
    std::string* out_message) {

    if (dry_run) {
        if (out_message) {
            *out_message = "DXVK removido: El juego vuelve al driver DirectX 11 de Windows [DRY-RUN]";
        }
        return true;
    }

    std::filesystem::path dir = ResolveGameDirectory(game_exe_path);
    if (dir.empty()) {
        if (out_message) {
            *out_message = "Error: Ruta de juego inválida o vacía";
        }
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        if (out_message) {
            *out_message = "DXVK removido: El juego vuelve al driver DirectX 11 de Windows";
        }
        return true;
    }

    std::filesystem::path dstD3d11 = dir / L"d3d11.dll";
    std::filesystem::path dstDxgi = dir / L"dxgi.dll";
    std::filesystem::path confPath = dir / L"dxvk.conf";
    std::filesystem::path bakD3d11 = dir / L"d3d11.dll.original.bak";
    std::filesystem::path bakDxgi = dir / L"dxgi.dll.original.bak";

    // 1. Delete DXVK files with SandboxGuard
    fs_.Remove(dstD3d11.wstring(), false);
    fs_.Remove(dstDxgi.wstring(), false);
    fs_.Remove(confPath.wstring(), false);

    // 2. Delete logs generated by DXVK (*_d3d11.log, *_dxgi.log, dxvk.log)
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::wstring fname = entry.path().filename().wstring();
        if (EndsWithInsensitive(fname, L"_d3d11.log") ||
            EndsWithInsensitive(fname, L"_dxgi.log") ||
            EndsWithInsensitive(fname, L"dxvk.log")) {
            fs_.Remove(entry.path().wstring(), false);
        }
    }

    // 3. Restore backups if they existed
    if (std::filesystem::exists(bakD3d11, ec)) {
        fs_.Copy(bakD3d11.wstring(), dstD3d11.wstring(), true, false);
        fs_.Remove(bakD3d11.wstring(), false);
    }

    if (std::filesystem::exists(bakDxgi, ec)) {
        fs_.Copy(bakDxgi.wstring(), dstDxgi.wstring(), true, false);
        fs_.Remove(bakDxgi.wstring(), false);
    }

    if (out_message) {
        *out_message = "DXVK removido: El juego vuelve al driver DirectX 11 de Windows";
    }
    return true;
}

bool DxvkManager::ConfigureApuProfile(const std::filesystem::path& targetDir, const ApuOptimizationPolicy& policy, bool dryRun) {
    if (targetDir.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path dir = targetDir;
    if (std::filesystem::exists(dir, ec) && std::filesystem::is_regular_file(dir, ec)) {
        dir = dir.parent_path();
    }

    if (!fs_.CanModifyGameDir(dir)) {
        return false;
    }

    if (dryRun) {
        return true;
    }

    std::filesystem::path confPath = dir / L"dxvk.conf";

    const std::vector<std::pair<std::string, std::string>> requiredSettings = {
        {"dxvk.enableAsync", "true"},
        {"dxvk.numCompilerThreads", std::to_string(policy.dxvk_compiler_threads)},
        {"dxvk.useEarlyDiscard", "true"},
        {"dxvk.pipelineLibrary", "true"},
        {"dxvk.enableGraphicsPipelineLibrary", "true"},
        {"dxvk.trackPipelineLifetime", "true"}
    };

    std::string newContent;
    if (!std::filesystem::exists(confPath, ec)) {
        std::ostringstream ss;
        ss << "# VegaFlow Universal - DXVK APU Optimization Profile\n";
        for (const auto& [key, val] : requiredSettings) {
            ss << key << " = " << val << "\n";
        }
        newContent = ss.str();
    } else {
        std::string existing;
        if (!fs_.Read(confPath.wstring(), existing)) return false;

        std::vector<std::string> lines;
        std::istringstream in(existing);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }

        std::vector<bool> keyApplied(requiredSettings.size(), false);
        std::ostringstream ss;
        bool hasHeader = false;

        for (const auto& originalLine : lines) {
            std::string trimmed = originalLine;
            size_t start = trimmed.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                ss << originalLine << "\n";
                continue;
            }
            trimmed = trimmed.substr(start);

            if (trimmed.find("# VegaFlow Universal - DXVK APU Optimization Profile") != std::string::npos) {
                hasHeader = true;
            }

            bool matched = false;
            for (size_t i = 0; i < requiredSettings.size(); ++i) {
                const auto& key = requiredSettings[i].first;
                if (trimmed.rfind(key, 0) == 0) {
                    size_t klen = key.size();
                    if (klen == trimmed.size() || trimmed[klen] == ' ' || trimmed[klen] == '=' || trimmed[klen] == '\t') {
                        ss << key << " = " << requiredSettings[i].second << "\n";
                        keyApplied[i] = true;
                        matched = true;
                        break;
                    }
                }
            }

            if (!matched) {
                ss << originalLine << "\n";
            }
        }

        std::string updatedBody = ss.str();
        std::ostringstream finalOutput;
        if (!hasHeader) {
            finalOutput << "# VegaFlow Universal - DXVK APU Optimization Profile\n";
        }
        finalOutput << updatedBody;

        for (size_t i = 0; i < requiredSettings.size(); ++i) {
            if (!keyApplied[i]) {
                finalOutput << requiredSettings[i].first << " = " << requiredSettings[i].second << "\n";
            }
        }

        newContent = finalOutput.str();
    }

    return fs_.Write(confPath.wstring(), newContent, false);
}

bool DxvkManager::ValidateDxvkApuProfile(
    const std::filesystem::path& targetDir,
    bool& asyncEnabled,
    uint32_t& compilerThreads) {

    asyncEnabled = false;
    compilerThreads = 0;

    if (targetDir.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path dir = targetDir;
    if (std::filesystem::exists(dir, ec) && std::filesystem::is_regular_file(dir, ec)) {
        dir = dir.parent_path();
    }

    std::filesystem::path confPath = dir / L"dxvk.conf";
    if (!std::filesystem::exists(confPath, ec) || !std::filesystem::is_regular_file(confPath, ec)) {
        return false;
    }

    std::string content;
    if (!fs_.Read(confPath.wstring(), content)) {
        return false;
    }

    if (content.empty()) {
        return false;
    }

    bool hasEarlyDiscard = false;
    bool hasPipelineLib = false;
    bool hasGpl = false;
    bool hasTrackLifetime = false;

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        line = line.substr(first);
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);

        size_t kEnd = k.find_last_not_of(" \t");
        if (kEnd != std::string::npos) k = k.substr(0, kEnd + 1);

        size_t vStart = v.find_first_not_of(" \t");
        size_t vEnd = v.find_last_not_of(" \t");
        if (vStart != std::string::npos && vEnd != std::string::npos) {
            v = v.substr(vStart, vEnd - vStart + 1);
        } else {
            v = "";
        }

        std::string vLower = v;
        for (char& c : vLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (k == "dxvk.enableAsync") {
            if (vLower == "true" || vLower == "1") {
                asyncEnabled = true;
            }
        } else if (k == "dxvk.numCompilerThreads") {
            try {
                compilerThreads = static_cast<uint32_t>(std::stoul(v));
            } catch (...) {
                compilerThreads = 0;
            }
        } else if (k == "dxvk.useEarlyDiscard") {
            if (vLower == "true" || vLower == "1") {
                hasEarlyDiscard = true;
            }
        } else if (k == "dxvk.pipelineLibrary") {
            if (vLower == "true" || vLower == "1") {
                hasPipelineLib = true;
            }
        } else if (k == "dxvk.enableGraphicsPipelineLibrary") {
            if (vLower == "true" || vLower == "1") {
                hasGpl = true;
            }
        } else if (k == "dxvk.trackPipelineLifetime") {
            if (vLower == "true" || vLower == "1") {
                hasTrackLifetime = true;
            }
        }
    }

    return (asyncEnabled && compilerThreads >= 1 && compilerThreads <= 16 && hasEarlyDiscard && hasPipelineLib && hasGpl && hasTrackLifetime);
}

bool DxvkManager::RemoveApuProfile(const std::filesystem::path& targetDir, bool dryRun) {
    if (targetDir.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path dir = targetDir;
    if (std::filesystem::exists(dir, ec) && std::filesystem::is_regular_file(dir, ec)) {
        dir = dir.parent_path();
    }

    if (!fs_.CanModifyGameDir(dir)) {
        return false;
    }

    if (dryRun) {
        return true;
    }

    std::filesystem::path confPath = dir / L"dxvk.conf";
    if (!std::filesystem::exists(confPath, ec)) {
        return true;
    }

    return fs_.Remove(confPath.wstring(), false);
}

} // namespace vegaflow
