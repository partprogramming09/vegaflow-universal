// Adversarial unit tests: the "Programas" model and its edits on an AppConfig copy: out-of-range enum values, the action the
// screen does not offer, thousands of exceptions, Unicode names and repeated edits.

#include "session/programs_screen.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::ProcessAction;
using vegaflow::app::AppConfig;

void Attack(const std::string& name, const std::function<std::string()>& body) {
    try {
        const std::string why = body();
        RecordResult(why.empty(), name, why);
    } catch (const std::exception& e) {
        RecordResult(false, name, std::string("threw: ") + e.what());
    } catch (...) {
        RecordResult(false, name, "threw an unknown exception");
    }
}

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

} // namespace

void Test81() {
    // Test 81: attacks on the Programas model and its edits
    Attack("Test 81.1: the seven categories are well formed: unique ids, filled texts, ids the editor knows, no promise of gain", [&]() -> std::string {
        const auto m = gs::BuildProgramsScreen(AppConfig{});
        if (m.categories.size() != 7) return "categories " + std::to_string(m.categories.size());
        std::set<std::string> ids;
        for (const auto& c : m.categories) {
            if (!ids.insert(c.id).second) return "duplicate id " + c.id;
            if (c.title.empty() || c.apps.empty()) return "empty text in " + c.id;
            const std::string t = LowerAscii(c.title + c.apps);
            if (t.find("fps") != std::string::npos || t.find("mejora") != std::string::npos || t.find("rápid") != std::string::npos) return "promise in " + c.id;
            AppConfig copy;
            if (!gs::SetCategoryAction(copy, c.id, ProcessAction::EcoOnly)) return "editor does not know shown id " + c.id;
        }
        if (m.no_exceptions_text.empty() || m.no_games_text.empty()) return "empty-state text empty";
        return "";
    });

    Attack("Test 81.2: each category edit changes only its own category", [&]() -> std::string {
        const auto ids = [&]() {
            std::vector<std::string> v;
            for (const auto& c : gs::BuildProgramsScreen(AppConfig{}).categories) v.push_back(c.id);
            return v;
        }();
        for (const auto& id : ids) {
            for (const ProcessAction a : {ProcessAction::DoNotTouch, ProcessAction::EcoOnly, ProcessAction::Throttle}) {
                AppConfig base;
                AppConfig c = base;
                gs::SetCategoryAction(c, id, a);
                const auto before = gs::BuildProgramsScreen(base);
                const auto after = gs::BuildProgramsScreen(c);
                for (size_t i = 0; i < before.categories.size(); ++i) {
                    const bool mine = before.categories[i].id == id;
                    if (!mine && before.categories[i].action != after.categories[i].action) return "editing " + id + " changed " + before.categories[i].id;
                    if (mine && after.categories[i].action != a) return "editing " + id + " did not take";
                }
            }
        }
        return "";
    });

    Attack("Test 81.3: an action outside the enum is refused by the edit itself (only 0..2 exist as choices)", [&]() -> std::string {
        AppConfig c;
        const ProcessAction before = c.process_policy.category_actions.music;
        if (gs::SetCategoryAction(c, "music", static_cast<ProcessAction>(99))) return "SetCategoryAction accepted action 99 and stored " + std::to_string(static_cast<int>(c.process_policy.category_actions.music));
        if (c.process_policy.category_actions.music != before) return "category changed by a refused action";
        gs::AddException(c, "Foo.exe");
        if (gs::SetExceptionAction(c, "Foo.exe", static_cast<ProcessAction>(99))) return "SetExceptionAction accepted action 99 and stored " + std::to_string(static_cast<int>(c.process_policy.exceptions[0].action));
        return "";
    });

    Attack("Test 81.4: BoostAsGame is refused for an exception just as it is for a category (the screen offers only three choices)", [&]() -> std::string {
        AppConfig c;
        if (gs::SetCategoryAction(c, "music", ProcessAction::BoostAsGame)) return "category accepted BoostAsGame";
        gs::AddException(c, "Foo.exe");
        if (gs::SetExceptionAction(c, "Foo.exe", ProcessAction::BoostAsGame)) return "exception accepted BoostAsGame (the Rust rules refuse it when saving)";
        return "";
    });

    Attack("Test 81.5: every action label that the screen can show is non-empty and distinct", [&]() -> std::string {
        std::set<std::string> labels;
        for (const ProcessAction a : {ProcessAction::DoNotTouch, ProcessAction::EcoOnly, ProcessAction::Throttle, ProcessAction::BoostAsGame}) {
            const std::string l = gs::ActionLabel(a);
            if (l.empty()) return "empty label for " + std::to_string(static_cast<int>(a));
            labels.insert(l);
        }
        return labels.size() == 4 ? "" : "labels not distinct";
    });

    Attack("Test 81.6: 3000 exceptions and games are listed in order, with their UTF-8 text intact", [&]() -> std::string {
        AppConfig c;
        for (int i = 0; i < 3000; ++i) {
            c.process_policy.exceptions.push_back({L"Prog" + std::to_wstring(i) + L"ñ日.exe", static_cast<ProcessAction>(i % 3)});
            c.user_custom_game_exes.push_back(L"Juego" + std::to_wstring(i) + L"ü.exe");
        }
        const auto m = gs::BuildProgramsScreen(c);
        if (m.exceptions.size() != 3000 || m.games.size() != 3000) return "sizes";
        if (m.exceptions[2999].exe != "Prog2999ñ日.exe" || m.games[0] != "Juego0ü.exe") return "text changed: " + m.exceptions[2999].exe;
        if (m.exceptions[7].action != ProcessAction::EcoOnly) return "action lost";
        return "";
    });

    Attack("Test 81.7: AddException refuses with a sentence the names the Rust rules refuse, never stores them, and works with a null error pointer", [&]() -> std::string {
        for (const std::string& name : {std::string(""), std::string(" "), std::string("explorer.exe"), std::string("..\\a.exe"), std::string("a.exe."), std::string("CON.exe"), std::string("a\x01.exe"),
                                        std::string(252, 'a') + ".exe", std::string("\xff\xfe.exe"), std::string("a.EXE\n")}) {
            AppConfig c;
            std::string err;
            if (gs::AddException(c, name, &err) || err.empty()) return "'" + name.substr(0, 20) + "' accepted or refused with no sentence";
            if (gs::AddException(c, name, nullptr) || gs::AddManualGame(c, name, nullptr)) return "accepted with null error '" + name.substr(0, 20) + "'";
            if (!c.process_policy.exceptions.empty() || !c.user_custom_game_exes.empty()) return "stored '" + name.substr(0, 20) + "'";
        }
        return "";
    });

    Attack("Test 81.8: Remove and SetExceptionAction find a program in any letter case, ignore folders and blanks, and report a miss as false", [&]() -> std::string {
        AppConfig c;
        gs::AddException(c, "Telegram.exe");
        gs::AddManualGame(c, "Mine.exe");
        if (gs::RemoveException(c, "") || gs::RemoveException(c, "nope.exe") || gs::RemoveManualGame(c, "") || gs::RemoveManualGame(c, "nope.exe")) return "removed something that was not there";
        if (gs::SetExceptionAction(c, "", ProcessAction::Throttle) || gs::SetExceptionAction(c, "nope.exe", ProcessAction::Throttle)) return "edited something that was not there";
        if (!gs::SetExceptionAction(c, "TELEGRAM.EXE", ProcessAction::Throttle) || c.process_policy.exceptions[0].action != ProcessAction::Throttle) return "did not find by another letter case";
        if (!gs::RemoveException(c, "tELEGRAM.exe") || !c.process_policy.exceptions.empty()) return "did not remove by another letter case";
        if (gs::RemoveException(c, "Telegram.exe")) return "removed twice";
        if (!gs::RemoveManualGame(c, "MINE.EXE") || !c.user_custom_game_exes.empty()) return "manual game not removed";
        return "";
    });

    Attack("Test 81.9: add, remove, add 2000 times leaves the list as it started and never duplicates", [&]() -> std::string {
        AppConfig c;
        for (int i = 0; i < 2000; ++i) {
            const std::string n = "p" + std::to_string(i % 13) + ".exe";
            gs::AddException(c, n);
            gs::AddException(c, n);
            if (std::count_if(c.process_policy.exceptions.begin(), c.process_policy.exceptions.end(), [&](const auto& e) { return std::wstring(n.begin(), n.end()) == e.exe_name; }) > 1) return "duplicate " + n;
            gs::RemoveException(c, n);
        }
        return c.process_policy.exceptions.empty() ? "" : "left " + std::to_string(c.process_policy.exceptions.size());
    });
}

} // namespace vftest
