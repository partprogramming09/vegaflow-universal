#include "session/exit_notice.hpp"

namespace vegaflow::session {

ExitNotice BuildExitNotice(const HistoryScreenModel& history) {
    ExitNotice n;
    for (const auto& r : history.rows) {
        if (r.state == "Activo") ++n.active;
    }
    if (n.active == 0) return n;
    n.ask = true;
    n.text = std::string("VegaFlow tiene ") + std::to_string(n.active) + (n.active == 1 ? " cambio puesto" : " cambios puestos") +
             " en tu equipo. Cerrar VegaFlow no los deshace: siguen puestos y podrás deshacerlos desde Historial al volver a abrirlo.\n\n"
             "Sí: devolver todo a como estaba y cerrar.\nNo: cerrar y dejarlos puestos.\nCancelar: seguir con VegaFlow abierto.";
    return n;
}

} // namespace vegaflow::session
