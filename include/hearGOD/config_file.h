#pragma once
#include "hearGOD/types.h"
#include <string>
#include <filesystem>

namespace hearGOD {

// Load/save AudioConfig as minimal JSON to ~/.config/hearGOD/config.json
// Format: flat key=value, one per line, no nesting — no external dep needed.
struct ConfigFile {
    static std::filesystem::path defaultPath();
    static bool load(AudioConfig& cfg, const std::filesystem::path& path);
    static bool save(const AudioConfig& cfg, const std::filesystem::path& path);
};

} // namespace hearGOD
