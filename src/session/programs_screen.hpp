#pragma once
// Model of the "Programas" screen: what VegaFlow does with each kind of program while a game runs, the exceptions for
// single programs and the games that are not detected on their own. Edits change an AppConfig copy; the caller saves
// it. The rules for a program name live in the Rust core (policy/config_rules.rs) and ConfigStore::Save is the
// authority; the checks here only give a plain reason early.

#include <string>
#include <vector>

#include "model/game.hpp"
#include "session/config.hpp"

namespace vegaflow::session {

struct ProgramCategoryRow {
    std::string id; // the key in config.json's process_category_actions
    std::string title;
    std::string apps;
    ProcessAction action = ProcessAction::DoNotTouch;
};

struct ProgramExceptionRow {
    std::string exe; // UTF-8, as the person typed it
    ProcessAction action = ProcessAction::DoNotTouch;
};

struct ProgramsScreenModel {
    std::vector<ProgramCategoryRow> categories;
    std::vector<ProgramExceptionRow> exceptions;
    std::vector<std::string> games; // user games, not the ones taken from Steam
    std::string no_exceptions_text;
    std::string no_games_text;
};

ProgramsScreenModel BuildProgramsScreen(const app::AppConfig& cfg);

// "Frenar", "Solo ahorro", "No tocar" (BoostAsGame: "Como un juego", not a choice on this screen).
const char* ActionLabel(ProcessAction action);

bool SetCategoryAction(app::AppConfig& cfg, const std::string& id, ProcessAction action);

// Each returns false when nothing changed; `error` then holds a sentence for the person when the name was the problem.
bool AddException(app::AppConfig& cfg, const std::string& text, std::string* error = nullptr);
bool SetExceptionAction(app::AppConfig& cfg, const std::string& exe, ProcessAction action);
bool RemoveException(app::AppConfig& cfg, const std::string& exe);
// Add and Remove change only user_custom_game_exes; run app::SyncSteamGamesToCustomExes afterwards.
bool AddManualGame(app::AppConfig& cfg, const std::string& text, std::string* error = nullptr);
bool RemoveManualGame(app::AppConfig& cfg, const std::string& exe);

} // namespace vegaflow::session
