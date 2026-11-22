#include "session/gui_flow.hpp"

namespace vegaflow::session {

CloseStep CloseFlow::OnClose(const HistoryScreenModel& history, std::string* question) {
    if (open_) return CloseStep::Ignore;
    const ExitNotice n = BuildExitNotice(history);
    if (!n.ask) return CloseStep::Close;
    open_ = true;
    if (question) *question = n.text;
    return CloseStep::ShowDialog;
}

bool CloseFlow::OnChoice(CloseChoice choice, const std::function<void()>& reset_all) {
    open_ = false;
    if (choice == CloseChoice::Cancel) return false;
    if (choice == CloseChoice::RestoreAndClose && reset_all) reset_all();
    return true;
}

PanicHotkeyPlan PlanPanicHotkey(const PanicHotkey& h) {
    if (!IsSafePanicHotkey(h)) return {};
    return {(h.ctrl ? kHotModControl : 0u) | (h.alt ? kHotModAlt : 0u) | (h.shift ? kHotModShift : 0u) | kHotModNoRepeat, h.vk};
}

std::string PanicHotkeyNotice(const PanicHotkey& h, bool registered) {
    if (registered) return "";
    return "El atajo " + h.label + " no está disponible (otro programa lo usa): el botón «Devolver todo» sigue funcionando.";
}

bool PanicGuard::TryBegin(std::chrono::steady_clock::time_point now) {
    if (running_ || (has_ended_ && now - ended_ < kPanicCooldown)) return false;
    running_ = true;
    return true;
}

void PanicGuard::End(std::chrono::steady_clock::time_point now) {
    running_ = false;
    has_ended_ = true;
    ended_ = now;
}

PadAction PollPad(PadNavigator& nav, const std::function<bool(uint16_t*)>& read) {
    uint16_t buttons = 0;
    if (!read || !read(&buttons)) {
        nav.Reset();
        return PadAction::None;
    }
    return nav.Step(buttons);
}

std::string RestartNotice(const ConfigRow& row) { return row.applies_live ? "" : "Se aplica al reiniciar VegaFlow"; }

std::string DoctorPanelText(const std::string& report) {
    return report.empty() ? "No se pudo leer nada del equipo. Prueba abriendo VegaFlow como administrador." : report;
}

} // namespace vegaflow::session
