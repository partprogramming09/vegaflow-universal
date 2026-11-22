#include "session/game_actions.hpp"

#include <filesystem>

#include "windows/text_convert.hpp"

namespace vegaflow::session {

using app::WideToUtf8;

namespace {

bool HasProfile(const SteamGameEntry& g) { return g.engine == GameEngineType::UnrealEngine4_5 || g.engine == GameEngineType::Unity; }

} // namespace

SteamGameEntry* GameActions::Find(uint32_t app_id) {
    for (auto& g : d_.games) {
        if (g.app_id == app_id) return &g;
    }
    return nullptr;
}

std::string GameActions::Said(const std::string& msg) {
    if (d_.audit) d_.audit(msg);
    return msg;
}

std::string GameActions::NotFound(uint32_t app_id) { return Said("Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam"); }

GamesScreenModel GameActions::Poll() const {
    return BuildGamesScreen(d_.games, d_.policy, d_.policy.safe_vram_pool_mb, d_.config.process_policy.excluded_game_exes);
}

void GameActions::RefreshFlags(uint32_t only_app_id) {
    if (!d_.inspect) return;
    for (auto& g : d_.games) {
        if ((only_app_id != 0 && g.app_id != only_app_id) || g.install_dir.empty()) continue;
        const SteamGameEntry now = d_.inspect(g);
        g.engine_preset_applied = now.engine_preset_applied;
        g.flip_model_applied = now.flip_model_applied;
        g.dxvk_installed = now.dxvk_installed;
    }
}

std::string GameActions::Rescan() {
    d_.games = d_.scan();
    app::SyncSteamGamesToCustomExes(d_.games, d_.config);
    return Said("Biblioteca Steam re-escaneada: " + std::to_string(d_.games.size()) + " juegos detectados");
}

std::string GameActions::Apply(uint32_t app_id) {
    SteamGameEntry* g = Find(app_id);
    if (!g) return NotFound(app_id);
    if (IsExcludedGame(d_.config.process_policy.excluded_game_exes, *g)) return Said("Este juego está en tu lista de excluidos: VegaFlow no escribe nada para él.");
    const auto rep = d_.steam.ApplyEngineUmaPreset(*g, d_.policy, d_.policy.safe_vram_pool_mb, false);
    if (rep.success) {
        g->engine_preset_applied = true;
        g->flip_model_applied = true;
    }
    return Said(rep.status_message);
}

std::string GameActions::Restore(uint32_t app_id) {
    SteamGameEntry* g = Find(app_id);
    if (!g) return NotFound(app_id);
    const auto rep = d_.steam.RestoreEngineOriginalConfig(*g, false);
    if (rep.success) g->engine_preset_applied = false;
    return Said(rep.status_message);
}

std::string GameActions::Exclude(uint32_t app_id) {
    SteamGameEntry* g = Find(app_id);
    if (!g) return Said("No se encontró ese juego.");
    const std::string exe = WideToUtf8(g->primary_exe_path);
    if (exe.empty()) return Said("No se encontró el programa de ese juego.");
    app::AppConfig candidate = d_.config;
    std::string err;
    if (!ExcludeGame(candidate, exe, &err)) return Said(err); // an invalid name stops here, before any file changes

    // The daemon may have applied the profile since the scan, so the flag is read from disk now. The list is saved first: if
    // that fails nothing else has changed, and once it is saved the daemon ignores the game whatever happens to its file.
    RefreshFlags(app_id);
    std::string why;
    if (!d_.store.Save(candidate, false, &why)) return Said(why + " (No se guardó: el juego no quedó excluido y sus archivos siguen igual.)");
    d_.config = candidate;
    std::string note;
    if (HasProfile(*g) && !g->has_anticheat && g->engine_preset_applied) {
        const auto rep = d_.steam.RestoreEngineOriginalConfig(*g, false);
        if (rep.success) g->engine_preset_applied = false;
        note = rep.success ? "Archivo original devuelto. " : "No se pudo devolver el archivo original: " + rep.status_message + ". ";
    }
    return Said(note + "Juego excluido: VegaFlow lo ignora desde ahora.");
}

std::string GameActions::Include(uint32_t app_id) {
    SteamGameEntry* g = Find(app_id);
    const std::string exe = g ? WideToUtf8(g->primary_exe_path) : "";
    app::AppConfig candidate = d_.config;
    if (exe.empty() || !IncludeGame(candidate, exe)) return Said("Ese juego no estaba excluido.");
    std::string why;
    if (!d_.store.Save(candidate, false, &why)) return Said(why + " (No se guardó.)");
    d_.config = candidate;
    return Said("Juego incluido de nuevo.");
}

// F2.6: VegaFlow no longer installs DXVK (no measured gain); this only takes out what an earlier version put there.
std::string GameActions::ToggleDxvk(uint32_t app_id) {
    SteamGameEntry* g = Find(app_id);
    if (!g) return NotFound(app_id);
    if (g->has_anticheat) return Said("Este juego usa anti-cheat: VegaFlow no cambia ni quita archivos en su carpeta.");
    if (!g->dxvk_installed) return Said("VegaFlow ya no instala DXVK: no hay nada que quitar en este juego.");
    std::string msg;
    if (d_.dxvk.RemoveDxvk(g->primary_exe_path, false, &msg)) g->dxvk_installed = false;
    return Said(msg);
}

} // namespace vegaflow::session
