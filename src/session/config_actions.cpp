#include "session/config_actions.hpp"

#include "session/config_screen.hpp"
#include "session/programs_screen.hpp"

namespace vegaflow::session {

using app::AppConfig;

std::string ConfigActions::Save(const AppConfig& candidate, const std::string& done) {
    std::string why;
    // Save checks every rule before it writes, so a refusal leaves the file as it was. The reason goes first: the
    // status bar cuts long text at the end and the reason is what the person must act on.
    if (!store_.Save(candidate, false, &why)) return why + " (No se guardó.)";
    config_ = candidate;
    if (audit_) audit_("Configuración: " + done);
    return done;
}

std::string ConfigActions::EditPrograms(const std::function<bool(AppConfig&, std::string*)>& change) {
    AppConfig candidate = config_;
    std::string err;
    if (!change(candidate, &err)) return err.empty() ? "Ese cambio no existe." : err;
    return Save(candidate, "Guardado en config.json.");
}

std::string ConfigActions::SetNumber(const std::string& id, const std::string& text) {
    AppConfig candidate = config_;
    std::string err;
    if (!ApplyConfigNumber(candidate, id, text, &err)) return err;
    return Save(candidate, "Guardado en config.json.");
}

std::string ConfigActions::SetToggle(const std::string& id, bool on) {
    AppConfig candidate = config_;
    if (!ApplyConfigToggle(candidate, id, on)) return "Ese ajuste no existe.";
    return Save(candidate, "Guardado en config.json.");
}

std::string ConfigActions::Reset() {
    AppConfig candidate = config_; // the games and the exceptions are not part of this screen and stay
    const AppConfig factory;
    candidate.poll_interval_ms = factory.poll_interval_ms;
    candidate.restrict_game_cpu_sets = factory.restrict_game_cpu_sets;
    candidate.memory = factory.memory;
    candidate.memory.is_uma_architecture = config_.memory.is_uma_architecture;
    candidate.process_policy.heavy_work_as_game = factory.process_policy.heavy_work_as_game;
    return Save(candidate, "Configuración restablecida a los valores de fábrica.");
}

std::string ConfigActions::SetCategoryAction(const std::string& id, int action) {
    return EditPrograms([&](AppConfig& c, std::string*) { return session::SetCategoryAction(c, id, static_cast<ProcessAction>(action)); });
}
std::string ConfigActions::AddException(const std::string& exe) {
    return EditPrograms([&](AppConfig& c, std::string* e) { return session::AddException(c, exe, e); });
}
std::string ConfigActions::SetExceptionAction(const std::string& exe, int action) {
    return EditPrograms([&](AppConfig& c, std::string*) { return session::SetExceptionAction(c, exe, static_cast<ProcessAction>(action)); });
}
std::string ConfigActions::RemoveException(const std::string& exe) {
    return EditPrograms([&](AppConfig& c, std::string*) { return session::RemoveException(c, exe); });
}
std::string ConfigActions::AddManualGame(const std::string& exe) {
    return EditPrograms([&](AppConfig& c, std::string* e) {
        if (!session::AddManualGame(c, exe, e)) return false;
        app::SyncSteamGamesToCustomExes(games_, c);
        return true;
    });
}
std::string ConfigActions::RemoveManualGame(const std::string& exe) {
    return EditPrograms([&](AppConfig& c, std::string*) {
        if (!session::RemoveManualGame(c, exe)) return false;
        app::SyncSteamGamesToCustomExes(games_, c);
        return true;
    });
}

} // namespace vegaflow::session
