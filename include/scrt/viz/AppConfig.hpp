#pragma once
#include <filesystem>
#include <string>

namespace scrt::viz {

/// Settings that outlive a run and do not belong to any one scene.
struct AppConfig {
    /// The user's own Anthropic API key. Theirs, not ours - the assistant bills their account.
    std::string api_key;
    /// Model the assistant asks for.
    std::string model = "claude-opus-5";
};

/// Where the config file lives: %APPDATA%\solar-cooker-rt\config.json on Windows.
///
/// Not next to the executable: the app is shipped as a zip a user unpacks wherever they like,
/// sometimes somewhere unwritable, and a key written into the program folder would be lost on
/// the next version anyway.
std::filesystem::path config_path();

/// Reads the config, returning defaults when the file is missing or unreadable.
///
/// Never throws: a corrupt config must not stop the app starting, and the worst case is that the
/// user re-enters their key.
AppConfig load_config();

/// Writes the config. Returns false when the file could not be written.
///
/// The file is created with no special permissions - it sits in the user's own roaming profile,
/// which is where Windows applications keep this sort of thing, and it is readable by anything
/// running as that user. It is NOT an encrypted secret store; the assistant panel says so.
bool save_config(const AppConfig& cfg);

} // namespace scrt::viz
