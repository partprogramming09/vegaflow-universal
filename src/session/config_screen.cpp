#include "session/config_screen.hpp"

#include <cstdint>

namespace vegaflow::session {

namespace {

constexpr uint64_t kBytesPerMb = 1024ULL * 1024ULL;
constexpr uint64_t kMaxWhole = 1000000000ULL; // no real setting is this large; keeps every product inside 64 bits

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

bool WholeNumber(const std::string& s, uint64_t* out) {
    if (s.empty() || s.size() > 10) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > kMaxWhole) return false;
    *out = v;
    return true;
}

// "1,5", "1.25", "2" -> milliseconds. At most one separator and three decimals.
bool SecondsToMs(const std::string& s, uint64_t* out) {
    const size_t sep = s.find_first_of(",.");
    std::string whole = s.substr(0, sep);
    std::string frac = sep == std::string::npos ? "" : s.substr(sep + 1);
    if (frac.size() > 3 || frac.find_first_of(",.") != std::string::npos) return false;
    if (whole.empty() && frac.empty()) return false; // "", "." and "," carry no number
    if (whole.empty()) whole = "0";
    uint64_t w = 0;
    if (!WholeNumber(whole, &w)) return false;
    uint64_t f = 0;
    if (!frac.empty()) {
        if (!WholeNumber(frac, &f)) return false;
        for (size_t i = frac.size(); i < 3; ++i) f *= 10;
    }
    const uint64_t ms = w * 1000 + f;
    if (ms > UINT32_MAX) return false;
    *out = ms;
    return true;
}

std::string MsToSeconds(uint32_t ms) {
    std::string out = std::to_string(ms / 1000);
    uint32_t rest = ms % 1000;
    if (rest != 0) {
        std::string frac = std::to_string(rest);
        frac.insert(0, 3 - frac.size(), '0');
        while (!frac.empty() && frac.back() == '0') frac.pop_back();
        out += "," + frac;
    }
    return out;
}

ConfigRow Number(const char* id, const char* name, const char* help, std::string value, const char* unit) {
    ConfigRow r;
    r.id = id;
    r.kind = ConfigRowKind::Number;
    r.name = name;
    r.help = help;
    r.value = std::move(value);
    r.unit = unit;
    return r;
}

ConfigRow Toggle(const char* id, const char* name, const char* help, bool on, const char* chip = "") {
    ConfigRow r;
    r.id = id;
    r.kind = ConfigRowKind::Toggle;
    r.name = name;
    r.help = help;
    r.on = on;
    r.chip = chip;
    return r;
}

} // namespace

ConfigScreenModel BuildConfigScreen(const app::AppConfig& cfg) {
    ConfigScreenModel m;
    m.groups.push_back({"Modo de uso", "", {Toggle("heavy_work_as_game", "Tratar los trabajos pesados como un juego",
                                                   "Compilar, renderizar o editar vídeo también recibe la optimización mientras dura.",
                                                   cfg.process_policy.heavy_work_as_game)}});
    m.groups.push_back({"Detección de juegos", "", {Number("poll_interval", "Revisar qué ventana está delante cada",
                                                           "Más corto detecta antes; más largo gasta menos.", MsToSeconds(cfg.poll_interval_ms), "segundos")}});
    m.groups.push_back({"Memoria compartida", "Solo con gráfica integrada",
                        {Number("min_free_ram", "Liberar memoria si quedan libres menos de", "Por debajo de esta cifra el juego empieza a sufrir.",
                                std::to_string(cfg.memory.min_free_ram_mb), "MB"),
                         Number("min_standby", "…y hay en caché al menos", "Si hay poca caché, liberar no sirve de nada.",
                                std::to_string(cfg.memory.min_standby_mb), "MB"),
                         Number("cooldown", "Esperar entre dos liberaciones", "Evita tirones por liberar demasiado seguido.",
                                std::to_string(cfg.memory.cooldown_seconds), "segundos"),
                         Number("max_disk_read", "No liberar si el juego lee del disco más de", "Está cargando: liberar ahora le haría daño.",
                                std::to_string(cfg.memory.max_game_disk_read_bytes_per_sec / kBytesPerMb), "MB/s")}});
    m.groups.push_back({"Avanzado", "Cambiar solo si sabes lo que haces",
                        {Toggle("restrict_cpu_sets", "Limitar el juego a sus núcleos",
                                "Usa CPU Sets para que el juego solo corra en los núcleos que VegaFlow elige. En la mayoría de equipos el juego rinde mejor con todos los núcleos.",
                                cfg.restrict_game_cpu_sets, "No recomendado")}});
    return m;
}

bool ApplyConfigNumber(app::AppConfig& cfg, const std::string& id, const std::string& text, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    const std::string t = Trim(text);
    uint64_t v = 0;
    if (id == "poll_interval") {
        if (!SecondsToMs(t, &v)) return fail("Escribe los segundos como un número, por ejemplo 1,5.");
        cfg.poll_interval_ms = static_cast<uint32_t>(v);
        return true;
    }
    if (id != "min_free_ram" && id != "min_standby" && id != "cooldown" && id != "max_disk_read") return fail("Ese ajuste no existe.");
    if (!WholeNumber(t, &v)) return fail("Escribe un número entero, sin signos ni letras.");
    if (id == "min_free_ram") cfg.memory.min_free_ram_mb = v;
    else if (id == "min_standby") cfg.memory.min_standby_mb = v;
    else if (id == "cooldown") cfg.memory.cooldown_seconds = static_cast<uint32_t>(v);
    else cfg.memory.max_game_disk_read_bytes_per_sec = v * kBytesPerMb;
    return true;
}

bool ApplyConfigToggle(app::AppConfig& cfg, const std::string& id, bool on) {
    if (id == "restrict_cpu_sets") cfg.restrict_game_cpu_sets = on;
    else if (id == "heavy_work_as_game") cfg.process_policy.heavy_work_as_game = on;
    else return false;
    return true;
}

} // namespace vegaflow::session
