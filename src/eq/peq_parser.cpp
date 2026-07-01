#include "hearGOD/peq_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace hearGOD {

namespace {

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Parse a float from the token stream; return false on failure.
bool parseFloat(std::istringstream& ss, float& out)
{
    std::string tok;
    if (!(ss >> tok)) return false;
    try { out = std::stof(tok); return true; }
    catch (...) { return false; }
}

// Advance stream past a literal token (case-insensitive); return false if mismatch.
bool expect(std::istringstream& ss, const std::string& expected)
{
    std::string tok;
    if (!(ss >> tok)) return false;
    return toLower(tok) == toLower(expected);
}

// Read the next token from stream into out; return false on EOF.
bool nextToken(std::istringstream& ss, std::string& out)
{
    return static_cast<bool>(ss >> out);
}

// Convert APO filter type string → FilterType; returns false on unknown type.
bool parseFilterType(const std::string& s, FilterType& out)
{
    std::string lo = toLower(s);
    if (lo == "pk"  || lo == "peak")             { out = FilterType::PEAK;      return true; }
    if (lo == "ls"  || lo == "lsc" || lo == "lsc2") { out = FilterType::LOWSHELF; return true; }
    if (lo == "hs"  || lo == "hsc" || lo == "hsc2") { out = FilterType::HIGHSHELF;return true; }
    if (lo == "lp"  || lo == "lpq")               { out = FilterType::LOWPASS;   return true; }
    if (lo == "hp"  || lo == "hpq")               { out = FilterType::HIGHPASS;  return true; }
    return false;
}

} // namespace

std::optional<PEQPreset> parsePEQ(const std::string& path, float /*sampleRate*/)
{
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[PEQ] Cannot open: " << path << "\n";
        return std::nullopt;
    }

    PEQPreset preset;
    std::string line;
    int lineNum = 0;

    while (std::getline(f, line)) {
        ++lineNum;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        std::istringstream ss(t);
        std::string keyword;
        if (!(ss >> keyword)) continue;
        std::string klo = toLower(keyword);

        // --- Preamp ---
        if (klo == "preamp:") {
            float db = 0.0f;
            if (!parseFloat(ss, db)) {
                std::cerr << "[PEQ] Line " << lineNum << ": bad Preamp value, skipping\n";
                continue;
            }
            // skip trailing "dB" token if present
            preset.preampDb = db;
            continue;
        }

        // --- Filter N: ON|OFF <type> Fc <freq> Hz [Gain <db> dB] [Q <q>] ---
        // "Filter" already consumed; next token is "N:" (number + colon), then ON/OFF
        if (klo.rfind("filter", 0) == 0) {
            std::string numTok;
            if (!nextToken(ss, numTok)) continue;  // skip "1:" / "2:" etc.

            std::string onoff;
            if (!nextToken(ss, onoff)) {
                std::cerr << "[PEQ] Line " << lineNum << ": expected ON/OFF, skipping\n";
                continue;
            }
            if (toLower(onoff) == "off") continue;  // disabled filter

            std::string typeStr;
            if (!nextToken(ss, typeStr)) {
                std::cerr << "[PEQ] Line " << lineNum << ": expected filter type, skipping\n";
                continue;
            }
            FilterType type;
            if (!parseFilterType(typeStr, type)) {
                std::cerr << "[PEQ] Line " << lineNum
                          << ": unknown filter type '" << typeStr << "', skipping\n";
                continue;
            }

            // "Fc" keyword + frequency + "Hz"
            if (!expect(ss, "Fc")) {
                std::cerr << "[PEQ] Line " << lineNum << ": expected 'Fc', skipping\n";
                continue;
            }
            float freq = 0.0f;
            if (!parseFloat(ss, freq)) {
                std::cerr << "[PEQ] Line " << lineNum << ": bad Fc value, skipping\n";
                continue;
            }
            std::string hzTok;
            ss >> hzTok;  // consume "Hz" (ignore if missing — some exports omit it)

            float gainDb = 0.0f;
            float Q      = 0.707f;  // sensible default for LP/HP Butterworth

            // Remaining tokens: "Gain <db> dB" and/or "Q <q>" in any order
            std::string tok;
            while (ss >> tok) {
                std::string tlo = toLower(tok);
                if (tlo == "gain") {
                    if (!parseFloat(ss, gainDb)) {
                        std::cerr << "[PEQ] Line " << lineNum << ": bad Gain value\n";
                    }
                    ss >> tok;  // consume "dB"
                } else if (tlo == "q") {
                    if (!parseFloat(ss, Q)) {
                        std::cerr << "[PEQ] Line " << lineNum << ": bad Q value\n";
                    }
                }
                // unknown tokens silently skipped
            }

            if (freq <= 0.0f || freq > 96000.0f) {
                std::cerr << "[PEQ] Line " << lineNum
                          << ": Fc=" << freq << " out of range, skipping\n";
                continue;
            }
            if (Q <= 0.0f) {
                std::cerr << "[PEQ] Line " << lineNum << ": Q<=0, skipping\n";
                continue;
            }

            preset.filters.push_back({ type, freq, gainDb, Q });
            continue;
        }

        // Unknown line — silently skip (handles "ChannelInfo:", "Notes:", etc.)
    }

    std::cout << "[PEQ] Loaded " << preset.filters.size()
              << " filter(s), preamp=" << preset.preampDb << " dB from " << path << "\n";
    return preset;
}

std::vector<BiquadCoeff> buildCoeffs(const PEQPreset& preset, float sampleRate)
{
    std::vector<BiquadCoeff> coeffs;
    coeffs.reserve(preset.filters.size());
    for (const auto& f : preset.filters)
        coeffs.push_back(makeBiquad(f.type, f.freqHz, f.gainDb, f.Q, sampleRate));
    return coeffs;
}

} // namespace hearGOD
