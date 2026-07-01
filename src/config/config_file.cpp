#include "hearGOD/config_file.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>

namespace hearGOD {

std::filesystem::path ConfigFile::defaultPath()
{
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
    return base / ".config" / "hearGOD" / "config.json";
}

// Minimal hand-rolled JSON writer/reader — flat object only.
// Format: { "key": value, ... } where value is string or number.

static std::string quoted(const std::string& s)
{
    return "\"" + s + "\"";
}

bool ConfigFile::save(const AudioConfig& cfg, const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    if (!f) {
        std::cerr << "[Config] Cannot write: " << path << "\n";
        return false;
    }
    f << "{\n";
    f << "  \"sofa_path\": "      << quoted(cfg.sofaPath)        << ",\n";
    f << "  \"eq_path\": "        << quoted(cfg.eqPath)          << ",\n";
    f << "  \"input_device\": "   << cfg.inputDevice             << ",\n";
    f << "  \"output_device\": "  << cfg.outputDevice            << ",\n";
    f << "  \"input_channels\": " << cfg.inputChannels           << ",\n";
    f << "  \"master_gain_db\": " << cfg.masterGainDb            << ",\n";
    f << "  \"lfe_gain_db\": "    << cfg.lfeGainDb               << ",\n";
    f << "  \"keep_alive\": "     << (cfg.keepAlive ? "true" : "false") << ",\n";
    f << "  \"swap_lr\": "        << (cfg.swapLR    ? "true" : "false") << "\n";
    f << "}\n";
    std::cout << "[Config] Saved to " << path << "\n";
    return true;
}

bool ConfigFile::load(AudioConfig& cfg, const std::filesystem::path& path)
{
    std::ifstream f(path);
    if (!f) return false;  // silent — no config file is normal first run

    std::string line;
    while (std::getline(f, line)) {
        // Strip whitespace
        auto trim = [](const std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n,");
            return (a == std::string::npos) ? std::string{} : s.substr(a, b - a + 1);
        };
        std::string t = trim(line);
        if (t.empty() || t[0] == '{' || t[0] == '}') continue;

        size_t colon = t.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(t.substr(0, colon));
        std::string val = trim(t.substr(colon + 1));

        // Strip quotes from key and value
        auto unquote = [](std::string s) {
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        };
        key = unquote(key);
        val = unquote(val);

        if      (key == "sofa_path")      cfg.sofaPath      = val;
        else if (key == "eq_path")        cfg.eqPath        = val;
        else if (key == "input_device")   cfg.inputDevice   = std::stoi(val);
        else if (key == "output_device")  cfg.outputDevice  = std::stoi(val);
        else if (key == "input_channels") cfg.inputChannels = std::stoi(val);
        else if (key == "master_gain_db") cfg.masterGainDb  = std::stof(val);
        else if (key == "lfe_gain_db")    cfg.lfeGainDb     = std::stof(val);
        else if (key == "keep_alive")     cfg.keepAlive = (val == "true");
        else if (key == "swap_lr")        cfg.swapLR    = (val == "true");
    }
    std::cout << "[Config] Loaded from " << path << "\n";
    return true;
}

} // namespace hearGOD
