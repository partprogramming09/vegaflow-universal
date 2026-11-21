// Unit tests: the copyable report of the "Algo va mal" card. Pure text from the Inicio model and the history rows.

#include "session/report.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

namespace {

namespace gs = vegaflow::session;

gs::ReportInputs Sample() {
    gs::ReportInputs in;
    in.version = "0.2.0";
    in.home.permission_text = "Permisos: administrador";
    in.home.game_title = "En espera";
    in.home.equipment = {{"Procesador", "AMD Ryzen 5 4600H"}, {"Gráfica", "AMD Radeon(TM) Graphics"}};
    in.home.memory_detail = "9000 MB disponibles de 15788 MB";
    in.home.guard_text = "El guardián está apagado";
    in.home.purge_text = "Aún no liberó memoria";
    in.home.background_text = "Ninguno por su cuenta";
    gs::HistoryRow r;
    r.id = 1;
    r.when = "08:30";
    r.what = "Perfil de energía";
    r.before = "Equilibrado";
    r.after = "Alto rendimiento";
    r.state = "Activo";
    in.history.rows.push_back(r);
    in.status = "Informe copiado";
    return in;
}

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

} // namespace

void Test84() {
    // 1. The report carries the version, the machine, the memory lines, each change with before and after, and the last notice.
    const std::string r = gs::BuildReport(Sample());
    const bool content = Has(r, "VegaFlow Universal 0.2.0") && Has(r, "AMD Ryzen 5 4600H") && Has(r, "AMD Radeon(TM) Graphics") &&
                         Has(r, "9000 MB disponibles de 15788 MB") && Has(r, "Perfil de energía") && Has(r, "Equilibrado") &&
                         Has(r, "Alto rendimiento") && Has(r, "Activo") && Has(r, "Informe copiado");
    RecordResult(content, "Test 84.1: the report names the version, the machine, memory, every change (before and after) and the last notice");

    // 2. Windows line endings only, and a field with a line break cannot start a fake line of its own.
    auto in = Sample();
    in.history.rows[0].what = "uno\r\ndos\nFalso: línea inventada";
    const std::string r2 = gs::BuildReport(in);
    bool lone_lf = false;
    for (size_t i = 0; i < r2.size(); ++i) lone_lf = lone_lf || (r2[i] == '\n' && (i == 0 || r2[i - 1] != '\r'));
    bool lone_cr = false;
    for (size_t i = 0; i < r2.size(); ++i) lone_cr = lone_cr || (r2[i] == '\r' && (i + 1 >= r2.size() || r2[i + 1] != '\n'));
    RecordResult(!lone_lf && !lone_cr && !Has(r2, "\r\nFalso: línea inventada"),
                 "Test 84.2: the report uses CRLF only and a line break inside a field does not make a new line");

    // 3. Only the newest changes go in, with the count said; an empty history says so.
    auto many = Sample();
    for (int i = 2; i <= 80; ++i) {
        gs::HistoryRow row = many.history.rows[0];
        row.id = static_cast<uint64_t>(i);
        row.what = "Cambio numero " + std::to_string(i);
        many.history.rows.push_back(row);
    }
    const std::string r3 = gs::BuildReport(many);
    size_t lines = 0;
    for (size_t p = r3.find("Cambio numero"); p != std::string::npos; p = r3.find("Cambio numero", p + 1)) ++lines;
    RecordResult(lines == 29 && Has(r3, "(80, los 30 más recientes)"), // row 1 says "Perfil de energía", rows 2..30 say "Cambio numero"
                 "Test 84.3: at most the 30 newest changes are listed, and the total is said",
                 "lines=" + std::to_string(lines));
    auto none = Sample();
    none.history.rows.clear();
    none.history.empty_text = "Todavía no se ha cambiado nada";
    RecordResult(Has(gs::BuildReport(none), "Todavía no se ha cambiado nada"), "Test 84.4: an empty history says so in the report");

    // 4. A long field is cut with "..." so one odd value cannot flood the report.
    auto big = Sample();
    big.home.equipment[0].second = std::string(5000, 'x');
    const std::string r5 = gs::BuildReport(big);
    RecordResult(r5.size() < 3000 && Has(r5, "..."), "Test 84.5: a 5000-character field is cut, not copied whole", "size=" + std::to_string(r5.size()));

    // 5. The report is pasted into public places: the Windows user name in any path must not leave the machine.
    auto priv = Sample();
    priv.history.rows[0].before = "Backup en C:\\Users\\Ana\\AppData\\Local\\X\\Engine.ini.vegaflow.bak";
    priv.history.rows[0].after = "d:/users/Beto Perez/Documents/y.ini";
    priv.status = "No se pudo escribir C:\\USERS\\Carla\\x.json";
    const std::string r6 = gs::BuildReport(priv);
    RecordResult(!Has(r6, "Ana") && !Has(r6, "Beto") && !Has(r6, "Perez") && !Has(r6, "Carla") && Has(r6, "AppData\\Local\\X\\Engine.ini.vegaflow.bak"),
                 "Test 84.6: the user name in a path is replaced, the rest of the path stays", r6);

    // 6. The cut at 200 bytes never splits a UTF-8 character (accents are common in the text).
    for (size_t pad : {197u, 198u, 199u, 200u}) {
        auto u = Sample();
        u.home.equipment[0].second = std::string(pad, 'x') + "ééé";
        const std::string r7 = gs::BuildReport(u);
        bool valid = true;
        for (size_t i = 0; i < r7.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(r7[i]);
            if (c >= 0xC0) { // lead byte: the next byte must be a continuation
                if (i + 1 >= r7.size() || (static_cast<unsigned char>(r7[i + 1]) & 0xC0) != 0x80) valid = false;
            } else if (c >= 0x80 && (i == 0 || static_cast<unsigned char>(r7[i - 1]) < 0x80)) {
                valid = false; // continuation byte after ASCII
            }
        }
        RecordResult(valid && Has(r7, "..."), "Test 84.7: a UTF-8 cut at pad " + std::to_string(pad) + " leaves valid text");
    }

    // 7. Exactly 30 changes are all listed with no "most recent" note; 31 adds it.
    auto n30 = Sample();
    for (int i = 2; i <= 30; ++i) n30.history.rows.push_back(n30.history.rows[0]);
    const std::string r8 = gs::BuildReport(n30);
    n30.history.rows.push_back(n30.history.rows[0]);
    const std::string r9 = gs::BuildReport(n30);
    RecordResult(Has(r8, "Cambios (30)") && !Has(r8, "más recientes") && Has(r9, "(31, los 30 más recientes)"), "Test 84.8: the 30-row limit is exact");
}

} // namespace vftest
