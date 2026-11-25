#pragma once
// Where config.json lives and whether accounts without administrator rights could have changed it.

#include <string>

namespace vegaflow {

// Folder of the running executable, also for paths longer than MAX_PATH. Empty if Windows does not give it.
std::wstring ExecutableDirectory();

// <folder of the running executable>\config\config.json, whatever the working folder is. Empty if Windows does not
// give the executable's path.
std::wstring ConfigFilePath();

// True when only SYSTEM, Administrators and TrustedInstaller can change the file or folder (owner and DACL; the rule
// is in the Rust core, policy/folder_trust.rs). False if the permissions cannot be read.
bool OnlyAdminsCanWrite(const std::wstring& path);

// The folder of the executable, the config folder and config.json itself (when it exists) all pass OnlyAdminsCanWrite.
// Folders above the executable's are not checked: an account that could swap them could also replace the executable.
bool ConfigLocationTrusted(const std::wstring& config_path);

} // namespace vegaflow
