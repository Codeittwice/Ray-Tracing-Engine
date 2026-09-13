#include "scrt/viz/AppConfig.hpp"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace scrt::viz {

std::filesystem::path config_path() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"))
        return std::filesystem::path(appdata) / "solar-cooker-rt" / "config.json";
#endif
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".solar-cooker-rt" / "config.json";
    return std::filesystem::path("solar-cooker-rt-config.json");
}

AppConfig load_config() {
    AppConfig cfg;
    try {
        std::ifstream in(config_path());
        if (!in) return cfg;
        json j;
        in >> j;
        cfg.api_key = j.value("api_key", cfg.api_key);
        cfg.model   = j.value("model", cfg.model);
    } catch (...) {
        // A corrupt config is not worth failing over; the user re-enters their key.
    }
    return cfg;
}

bool save_config(const AppConfig& cfg) {
    try {
        const auto path = config_path();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        out << json{{"api_key", cfg.api_key}, {"model", cfg.model}}.dump(2) << "\n";
        return out.good();
    } catch (...) {
        return false;
    }
}

} // namespace scrt::viz
