#pragma once
#include "hearGOD/biquad_chain.h"
#include <string>
#include <vector>
#include <optional>

namespace hearGOD {

// Parses Equalizer APO ParametricEQ text format (what AutoEQ exports).
//
// Supported lines:
//   Preamp: -6.1 dB
//   Filter 1: ON PK  Fc 32 Hz  Gain 4.0 dB  Q 1.41
//   Filter 2: ON LS  Fc 105 Hz Gain -3.5 dB Q 0.9
//   Filter 3: ON HS  Fc 10000 Hz Gain 2.0 dB Q 0.71
//   Filter 4: ON LP  Fc 20000 Hz
//   Filter 5: ON HP  Fc 20 Hz
//   Filter 6: OFF PK Fc 1000 Hz Gain 0 dB Q 1.0   ← skipped
//   # comment lines
//
// Type strings (case-insensitive): PK, PEAK, LS, LSC, HS, HSC, LP, LPQ, HP, HPQ

struct PEQFilter {
    FilterType type;
    float freqHz;
    float gainDb;  // unused for LP/HP
    float Q;
};

struct PEQPreset {
    float preampDb = 0.0f;
    std::vector<PEQFilter> filters;
};

// Returns nullopt on file open failure; parse errors are logged + skipped.
std::optional<PEQPreset> parsePEQ(const std::string& path, float sampleRate = 48000.0f);

// Build BiquadCoeff vector from a preset (ready to pass to BiquadChain::setFilters)
std::vector<BiquadCoeff> buildCoeffs(const PEQPreset& preset, float sampleRate = 48000.0f);

} // namespace hearGOD
