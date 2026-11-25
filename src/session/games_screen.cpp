#include "session/games_screen.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>

#include "bridge/rust_bridge.h"
#include "ports/text_convert.hpp"

namespace vegaflow::session {

using app::Utf8ToWide;
using app::WideToUtf8;

namespace {

bool SameName(const std::wstring& a, const std::wstring& b) { return app::EqualsIgnoreCase(a, b); }

std::wstring FileName(const std::wstring& path) { return std::filesystem::path(path).filename().wstring(); }

const char* EngineLabel(GameEngineType t) {
    switch (t) {
        case GameEngineType::UnrealEngine4_5: return "Unreal Engine 4/5";
        case GameEngineType::Unity: return "Unity";
        case GameEngineType::Source1_2: return "Source";
        case GameEngineType::NativeDirectX_Vulkan: return "DirectX o Vulkan propio";
        default: return "Motor no reconocido";
    }
}

// The registry value ApplyEngineUmaPreset writes first for every game with an exe (DxgiFlipTweaks).
std::string RegistryChange(const std::wstring& exe_path) {
    return "Registro, HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences, valor " + WideToUtf8(exe_path) +
           ": SwapEffectUpgradeEnable=1; (se mezcla con lo que ya hubiera en ese valor; tu elección de gráfica y de HDR no cambia)";
}

// Keys of the Unreal block as written to Engine.ini: the lines of GenerateUnrealUmaBlock that set something.
std::vector<std::string> UnrealKeys(const ApuOptimizationPolicy& policy, uint32_t pool_mb) {
    const std::string block = SteamEngineOptimizer::GenerateUnrealUmaBlock(policy, pool_mb);
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < block.size()) {
        size_t eol = block.find('\n', pos);
        if (eol == std::string::npos) eol = block.size();
        std::string line = block.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] != ';' && line[0] != '[' && line.find('=') != std::string::npos) out.push_back(line);
    }
    return out;
}

} // namespace

const char* StatusLabel(GameStatus status) {
    switch (status) {
        case GameStatus::NotApplied: return "Sin cambios";
        case GameStatus::Applied: return "Perfil aplicado";
        case GameStatus::Protected: return "Protegido";
        case GameStatus::NoProfile: return "Sin perfil";
        case GameStatus::Excluded: return "Excluido";
    }
    return "";
}

GamesScreenModel BuildGamesScreen(const std::vector<SteamGameEntry>& games, const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb,
                                  const std::vector<std::wstring>& excluded) {
    GamesScreenModel m;
    m.empty_text = "VegaFlow no encontró juegos de Steam instalados. Los juegos de otras tiendas se agregan a mano en Programas.";
    for (const auto& g : games) {
        GameRow r;
        r.app_id = g.app_id;
        r.name = g.name;
        r.engine = EngineLabel(g.engine);
        const std::wstring exe = FileName(g.primary_exe_path);
        r.exe = WideToUtf8(exe);
        r.excluded = IsExcludedGame(excluded, g);
        const bool has_profile = g.engine == GameEngineType::UnrealEngine4_5 || g.engine == GameEngineType::Unity;
        const bool unreal = g.engine == GameEngineType::UnrealEngine4_5;
        r.backup_hint = "Antes de cambiar el archivo, VegaFlow guarda una copia con el mismo nombre y la terminación .vegaflow.bak, en la misma carpeta.";

        // What the daemon does on its own when the game opens (session/game_session.cpp): the registry value for every game, and
        // the Unreal profile for Unreal games without anti-cheat. Unity and the buttons here are manual.
        if (r.excluded) {
            r.status = GameStatus::Excluded;
            r.detail = "Está en tu lista de juegos excluidos: VegaFlow lo ignora y no escribe nada para él. Puedes volver a incluirlo.";
            r.can_restore = g.engine_preset_applied && has_profile && !g.has_anticheat;
        } else if (g.has_anticheat) {
            r.status = GameStatus::Protected;
            r.detail = "Este juego usa anti-cheat. VegaFlow no cambia sus archivos; al abrirlo solo escribe el valor de registro de abajo.";
        } else if (!has_profile || g.primary_exe_path.empty()) {
            r.status = GameStatus::NoProfile;
            r.detail = g.primary_exe_path.empty() ? "VegaFlow no encontró el programa de este juego y no cambia nada en él."
                                                  : "VegaFlow no tiene un perfil de archivos para este motor; al abrirlo solo escribe el valor de registro de abajo.";
        } else if (g.engine_preset_applied) {
            r.status = GameStatus::Applied;
            r.detail = "VegaFlow ya cambió el archivo de este juego. «Volver al archivo original» lo deja como estaba; el valor del registro no se toca.";
            r.can_restore = true;
        } else {
            r.status = GameStatus::NotApplied;
            r.detail = unreal ? "Al abrir este juego, VegaFlow aplica el perfil solo. También puedes aplicarlo ahora."
                              : "VegaFlow no aplica este perfil solo: se aplica cuando pulsas Aplicar perfil.";
            r.can_apply = true;
        }

        if (has_profile && !g.has_anticheat && !r.excluded && !g.primary_exe_path.empty()) {
            if (unreal) {
                r.file_hint = "Engine.ini del juego, en su carpeta de configuración";
                r.changes = UnrealKeys(policy, vram_pool_mb);
            } else {
                r.file_hint = "boot.config, en la carpeta de datos del juego (la que termina en _Data)";
                for (const auto& [k, v] : SteamEngineOptimizer::UnityBootConfigKeys()) r.changes.push_back(k + "=" + v);
            }
        } else {
            r.backup_hint.clear();
        }
        if (!r.excluded && !g.primary_exe_path.empty()) r.changes.push_back(RegistryChange(g.primary_exe_path));
        r.dxvk_installed = g.dxvk_installed;
        // F2.6: only removal is offered, for a DXVK an earlier version installed; also for an excluded game (the one that may have gone wrong).
        r.can_dxvk = !g.has_anticheat && !g.primary_exe_path.empty() && g.dxvk_installed;
        if (r.can_dxvk) {
            r.dxvk_hint = "Una versión anterior de VegaFlow instaló DXVK en este juego (d3d11.dll, dxgi.dll y dxvk.conf junto al programa). Quitarlo retira DXVK y devuelve los archivos que había antes. VegaFlow ya no lo instala.";
        }
        m.rows.push_back(std::move(r));
    }
    std::stable_sort(m.rows.begin(), m.rows.end(), [](const GameRow& a, const GameRow& b) { return a.name < b.name; });
    return m;
}

bool IsExcludedGame(const std::vector<std::wstring>& excluded, const SteamGameEntry& game) {
    const std::wstring exe = FileName(game.primary_exe_path);
    return !exe.empty() && std::any_of(excluded.begin(), excluded.end(), [&](const std::wstring& x) { return SameName(x, exe); });
}

bool ExcludeGame(app::AppConfig& cfg, const std::string& exe_or_path, std::string* error) {
    if (error) error->clear();
    if (!app::IsValidUtf8(exe_or_path)) {
        if (error) *error = "Ese nombre no es un texto válido.";
        return false;
    }
    const std::wstring name = FileName(Utf8ToWide(exe_or_path));
    const std::string utf8 = WideToUtf8(name);
    if (name.empty() || !vf_rust_is_allowed_game_exe(utf8.data(), utf8.size())) {
        if (error) *error = "Ese nombre es de un proceso de Windows o no es un nombre de archivo válido.";
        return false;
    }
    auto& list = cfg.process_policy.excluded_game_exes;
    if (std::any_of(list.begin(), list.end(), [&](const std::wstring& x) { return SameName(x, name); })) {
        if (error) *error = "Ese juego ya está excluido.";
        return false;
    }
    list.push_back(name);
    return true;
}

bool IncludeGame(app::AppConfig& cfg, const std::string& exe_or_path) {
    const std::wstring name = FileName(Utf8ToWide(exe_or_path));
    auto& list = cfg.process_policy.excluded_game_exes;
    const auto it = std::find_if(list.begin(), list.end(), [&](const std::wstring& x) { return SameName(x, name); });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

} // namespace vegaflow::session
