#include "session/programs_screen.hpp"

#include <algorithm>
#include <cwctype>

#include "bridge/rust_bridge.h"
#include "windows/text_convert.hpp"

namespace vegaflow::session {

using app::Utf8ToWide;
using app::WideToUtf8;

namespace {

struct CategorySpec {
    const char* id;
    const char* title;
    const char* apps;
    ProcessAction ProcessCategoryActions::*field;
};

// The names are the ones classify() in the Rust core recognises (rust/vegaflow-core/src/policy/process_class.rs; docs/guides/modules.md).
const CategorySpec kCategories[] = {
    {"recording_streaming", "Grabación y transmisión", "OBS Studio, Streamlabs, Overwolf", &ProcessCategoryActions::recording_streaming},
    {"calls_voice", "Llamadas y voz", "Discord, Microsoft Teams, Skype", &ProcessCategoryActions::calls_voice},
    {"messaging", "Mensajería", "Slack, Telegram, WhatsApp", &ProcessCategoryActions::messaging},
    {"browsers", "Navegadores", "Chrome, Edge, Firefox, Brave, Opera, Vivaldi", &ProcessCategoryActions::browsers},
    {"stores_launchers", "Tiendas y lanzadores", "Steam (tienda), Epic, Battle.net, EA, Ubisoft Connect, GOG, Riot", &ProcessCategoryActions::stores_launchers},
    {"music", "Música", "Spotify", &ProcessCategoryActions::music},
    {"cloud_sync", "Sincronización en la nube", "OneDrive, Google Drive, Dropbox", &ProcessCategoryActions::cloud_sync},
};

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

bool SameName(const std::wstring& a, const std::wstring& b) { return app::EqualsIgnoreCase(a, b); }

// The three choices the screen offers. BoostAsGame is not one of them, and no other number is an action.
bool IsChoice(ProcessAction a) { return a == ProcessAction::DoNotTouch || a == ProcessAction::EcoOnly || a == ProcessAction::Throttle; }

// A bare file name ending in .exe, like the daemon expects. The Rust rules decide the rest when saving.
bool CheckName(const std::string& text, std::wstring* out, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (!app::IsValidUtf8(text)) return fail("Ese nombre no es un texto válido.");
    const std::string t = Trim(text);
    if (t.empty()) return fail("Escribe el nombre del programa, por ejemplo juego.exe.");
    if (t.find_first_of("\\/:*?\"<>|") != std::string::npos) return fail("Escribe solo el nombre del archivo, sin carpeta, por ejemplo juego.exe.");
    const std::wstring w = Utf8ToWide(t);
    if (w.size() < 5 || !SameName(w.substr(w.size() - 4), L".exe")) return fail("El nombre debe terminar en .exe, por ejemplo juego.exe.");
    // The Rust rules decide: a bare file name Windows reads as written, not a Windows process, not a reserved device name.
    if (!vf_rust_is_allowed_game_exe(t.data(), t.size())) {
        return fail("Ese nombre es de un proceso de Windows o no es un nombre de archivo válido.");
    }
    *out = w;
    return true;
}

template <typename Vec>
typename Vec::iterator FindName(Vec& v, const std::wstring& name) {
    return std::find_if(v.begin(), v.end(), [&](const std::wstring& x) { return SameName(x, name); });
}

} // namespace

const char* ActionLabel(ProcessAction action) {
    switch (action) {
        case ProcessAction::Throttle: return "Frenar";
        case ProcessAction::EcoOnly: return "Solo ahorro";
        case ProcessAction::DoNotTouch: return "No tocar";
        case ProcessAction::BoostAsGame: return "Como un juego";
    }
    return "";
}

ProgramsScreenModel BuildProgramsScreen(const app::AppConfig& cfg) {
    ProgramsScreenModel m;
    for (const auto& c : kCategories) {
        m.categories.push_back({c.id, c.title, c.apps, cfg.process_policy.category_actions.*c.field});
    }
    for (const auto& e : cfg.process_policy.exceptions) m.exceptions.push_back({WideToUtf8(e.exe_name), e.action});
    for (const auto& g : cfg.user_custom_game_exes) m.games.push_back(WideToUtf8(g));
    m.no_exceptions_text = "Aún no hay excepciones: todos los programas siguen la regla de su tipo.";
    m.no_games_text = "Aún no añadiste ningún juego a mano. Los de Steam se detectan solos.";
    return m;
}

bool SetCategoryAction(app::AppConfig& cfg, const std::string& id, ProcessAction action) {
    if (!IsChoice(action)) return false;
    for (const auto& c : kCategories) {
        if (id == c.id) {
            cfg.process_policy.category_actions.*c.field = action;
            return true;
        }
    }
    return false;
}

bool AddException(app::AppConfig& cfg, const std::string& text, std::string* error) {
    std::wstring name;
    if (!CheckName(text, &name, error)) return false;
    auto& list = cfg.process_policy.exceptions;
    const bool dup = std::any_of(list.begin(), list.end(), [&](const ProcessExceptionEntry& e) { return SameName(e.exe_name, name); });
    if (dup) {
        if (error) *error = "Ese programa ya tiene una excepción.";
        return false;
    }
    list.push_back({name, ProcessAction::DoNotTouch});
    return true;
}

bool SetExceptionAction(app::AppConfig& cfg, const std::string& exe, ProcessAction action) {
    if (!IsChoice(action)) return false;
    const std::wstring name = Utf8ToWide(exe);
    for (auto& e : cfg.process_policy.exceptions) {
        if (SameName(e.exe_name, name)) {
            e.action = action;
            return true;
        }
    }
    return false;
}

bool RemoveException(app::AppConfig& cfg, const std::string& exe) {
    const std::wstring name = Utf8ToWide(exe);
    auto& list = cfg.process_policy.exceptions;
    const auto it = std::find_if(list.begin(), list.end(), [&](const ProcessExceptionEntry& e) { return SameName(e.exe_name, name); });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

// Only the user's list changes here: the lists the daemon reads (the user's games plus the Steam ones) are rebuilt by
// SyncSteamGamesToCustomExes, which the caller runs after every change so a Steam game with the same name stays detected.
bool AddManualGame(app::AppConfig& cfg, const std::string& text, std::string* error) {
    std::wstring name;
    if (!CheckName(text, &name, error)) return false;
    if (FindName(cfg.user_custom_game_exes, name) != cfg.user_custom_game_exes.end()) {
        if (error) *error = "Ese juego ya está en la lista.";
        return false;
    }
    if (FindName(cfg.process_policy.excluded_game_exes, name) != cfg.process_policy.excluded_game_exes.end()) {
        if (error) *error = "Ese juego está en tu lista de juegos excluidos, y VegaFlow lo ignora.";
        return false;
    }
    cfg.user_custom_game_exes.push_back(name);
    return true;
}

bool RemoveManualGame(app::AppConfig& cfg, const std::string& exe) {
    const auto it = FindName(cfg.user_custom_game_exes, Utf8ToWide(exe));
    if (it == cfg.user_custom_game_exes.end()) return false;
    cfg.user_custom_game_exes.erase(it);
    return true;
}

} // namespace vegaflow::session
