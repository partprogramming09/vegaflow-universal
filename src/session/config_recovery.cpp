#include "session/config_recovery.hpp"

namespace vegaflow::app {

ConfigRecoveryOffer RecoveryOffer(const ConfigLoadReport& report) {
    switch (report.Problem()) {
        case ConfigProblem::Corrupt:
        case ConfigProblem::Rejected:
        case ConfigProblem::NewerVersion: return {true, true};
        case ConfigProblem::Unreadable: return {false, true}; // no permission: it cannot be copied either
        default: return {};                                   // nothing wrong, or a folder only an administrator can fix
    }
}

std::string ResolveConfigProblem(ConfigStore& store, AppConfig& config, const ConfigRecoveryDeps& deps, bool dry_run) {
    const ConfigRecoveryOffer offer = RecoveryOffer(store.Report());
    if (!offer.factory_reset && !offer.open_file) return "";
    const ConfigChoice choice = deps.ask ? deps.ask(store.Report(), offer) : ConfigChoice::Dismiss;
    if (choice == ConfigChoice::OpenFile && offer.open_file) {
        if (deps.open_file && deps.open_file(store.Path())) return "Se abrió config.json: corrígelo y reinicia VegaFlow.";
        return "No se pudo abrir config.json: está junto a vegaflow_daemon.exe.";
    }
    if (choice == ConfigChoice::FactoryReset && offer.factory_reset) {
        std::string why;
        if (!store.ResetToFactory(dry_run, &why)) return "Valores de fábrica no aplicados: " + why + ".";
        if (!dry_run) config = store.Load();
        return dry_run ? "Valores de fábrica: comprobado, no se escribió nada."
                       : "config.json vuelve a los valores de fábrica; la copia anterior quedó junto al archivo (.vegaflow.bak).";
    }
    return "";
}

} // namespace vegaflow::app
