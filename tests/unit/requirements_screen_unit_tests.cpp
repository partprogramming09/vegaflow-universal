// Unit tests: the model of the "Requisitos y advertencias" screen (phase G): what VegaFlow needs, whether this PC
// meets it (from what the probe read), and the warnings. A reading that failed is "No se pudo leer", never a pass.

#include "session/requirements_screen.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

namespace {

namespace rq = vegaflow::session;

vegaflow::SystemInfoSnapshot Win11() {
    vegaflow::SystemInfoSnapshot s;
    s.os_kernel.os_name = "Windows 11";
    s.os_kernel.build_number = 22631;
    s.os_kernel.full_version_string = "Windows 11 Build 22631";
    s.os_kernel.system_arch = "x64";
    s.gpu.adapter_name = "AMD Radeon(TM) Graphics";
    s.gpu.connection = "Integrada";
    return s;
}

const rq::RequirementRow* Row(const rq::RequirementsScreenModel& m, const std::string& id) {
    for (const auto& r : m.rows) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

} // namespace

void Test68() {
    // 1. A normal PC: the five requirements in order, the four mandatory ones met, Steam optional and found.
    const auto m = rq::BuildRequirementsScreen(Win11(), true, true);
    bool order = m.rows.size() == 5;
    const char* ids[] = {"windows", "permissions", "processor", "graphics", "steam"};
    for (size_t i = 0; order && i < 5; ++i) order = m.rows[i].id == ids[i] && !m.rows[i].what.empty() && !m.rows[i].detail.empty();
    RecordResult(order && Row(m, "windows")->state == rq::RequirementState::Meets && Row(m, "permissions")->state == rq::RequirementState::Meets &&
                     Row(m, "processor")->state == rq::RequirementState::Meets && Row(m, "graphics")->state == rq::RequirementState::Meets &&
                     Row(m, "steam")->state == rq::RequirementState::Meets && Row(m, "windows")->detail.find("Windows 11 Build 22631") != std::string::npos,
                 "Test 68.1: a Windows 11 x64 PC with admin rights, graphics and Steam meets all five, and the Windows row names the real version");

    // 2. Windows older than 10 version 1709 (build 16299) does not meet; the limit itself does; no reading is not a pass.
    auto windows_state = [](uint32_t build) {
        vegaflow::SystemInfoSnapshot s = Win11();
        s.os_kernel.build_number = build;
        return Row(rq::BuildRequirementsScreen(s, true, true), "windows")->state;
    };
    RecordResult(windows_state(15063) == rq::RequirementState::Limited && windows_state(16298) == rq::RequirementState::Limited &&
                     windows_state(16299) == rq::RequirementState::Meets && windows_state(0) == rq::RequirementState::Unknown,
                 "Test 68.2: build 16299 (Windows 10 1709) is the first that meets; older is limited (not measured there); no build read is unknown");

    // 3. Without administrator rights the row says what is lost, and is not met.
    const auto user = rq::BuildRequirementsScreen(Win11(), false, true);
    RecordResult(Row(user, "permissions")->state == rq::RequirementState::Missing && Row(user, "permissions")->detail.find("administrador") != std::string::npos,
                 "Test 68.3: without administrator rights the Permisos row is missing and says administrator");

    // 3b. Unreadable Windows says so; an adapter without a name still gets a sentence.
    vegaflow::SystemInfoSnapshot blind = Win11();
    blind.os_kernel.build_number = 0;
    blind.gpu.adapter_name = "";
    const auto bm = rq::BuildRequirementsScreen(blind, true, true);
    RecordResult(Row(bm, "windows")->detail.find("No se pudo leer") != std::string::npos && Row(bm, "graphics")->detail.find("sin nombre") != std::string::npos,
                 "Test 68.3b: an unreadable Windows version and an unnamed adapter are said in plain words");

    // 4. Processor: x64 meets, ARM is limited to showing information, unreadable is unknown.
    auto cpu_state = [](const char* arch) {
        vegaflow::SystemInfoSnapshot s = Win11();
        s.os_kernel.system_arch = arch;
        return Row(rq::BuildRequirementsScreen(s, true, true), "processor")->state;
    };
    RecordResult(cpu_state("x64") == rq::RequirementState::Meets && cpu_state("ARM64") == rq::RequirementState::Limited &&
                     cpu_state("ARM64 (x64 emulado)") == rq::RequirementState::Limited && cpu_state("x86") == rq::RequirementState::Missing &&
                     cpu_state("No se pudo leer") == rq::RequirementState::Unknown && cpu_state("") == rq::RequirementState::Unknown,
                 "Test 68.4: x64 meets, ARM (even emulated x64) is limited, x86 is missing, an unreadable architecture is unknown");

    // 5. Graphics: any readable kind meets (what it enables depends on the kind); unreadable is unknown.
    auto gpu_state = [](const char* connection) {
        vegaflow::SystemInfoSnapshot s = Win11();
        s.gpu.connection = connection;
        return Row(rq::BuildRequirementsScreen(s, true, true), "graphics")->state;
    };
    RecordResult(gpu_state("Integrada") == rq::RequirementState::Meets && gpu_state("Dedicada") == rq::RequirementState::Meets &&
                     gpu_state("Externa") == rq::RequirementState::Meets && gpu_state("No se pudo leer") == rq::RequirementState::Unknown,
                 "Test 68.5: integrated, dedicated and external graphics meet; an unreadable one is unknown");

    // 6. Steam is optional: found meets, not found is Optional and points to Programas.
    const auto no_steam = rq::BuildRequirementsScreen(Win11(), true, false);
    RecordResult(Row(no_steam, "steam")->state == rq::RequirementState::Optional && Row(no_steam, "steam")->detail.find("Programas") != std::string::npos,
                 "Test 68.6: without Steam the row is Optional and points to Programas");

    // 7. Labels for the states are all different and not empty, and the warnings promise no gain.
    std::string labels;
    bool distinct = true;
    for (auto st : {rq::RequirementState::Meets, rq::RequirementState::Missing, rq::RequirementState::Limited, rq::RequirementState::Unknown, rq::RequirementState::Optional}) {
        const std::string l = rq::StateLabel(st);
        distinct = distinct && !l.empty() && labels.find("|" + l + "|") == std::string::npos;
        labels += "|" + l + "|";
    }
    std::string all;
    for (const auto& w : m.warnings) all += w + " ";
    RecordResult(distinct && m.warnings.size() >= 3 && all.find("FPS") == std::string::npos && all.find("más rápido") == std::string::npos &&
                     all.find("no promete") != std::string::npos && all.find("no se puede deshacer") != std::string::npos,
                 "Test 68.7: the five state labels differ, there are warnings, and they say VegaFlow promises no result");
}

} // namespace vftest
