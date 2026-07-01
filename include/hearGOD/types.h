#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace hearGOD {

constexpr int SAMPLE_RATE       = 48000;
constexpr int BUFFER_FRAMES     = 256;      // default 5.33ms latency
constexpr int MAX_BUFFER_FRAMES = 1024;     // stack limit for RT arrays
constexpr int MAX_CHANNELS      = 16;       // BlackHole 16ch

// ITU-R BS.2051 channel IDs for 7.1 bed
enum class Channel : uint8_t {
    FL  = 0,  // Front Left
    FR  = 1,  // Front Right
    FC  = 2,  // Front Center
    LFE = 3,  // Low Frequency Effects
    BL  = 4,  // Back Left (Ls)
    BR  = 5,  // Back Right (Rs)
    SL  = 6,  // Side Left
    SR  = 7,  // Side Right
    TFL = 8,  // Top Front Left
    TFR = 9,  // Top Front Right
    TBL = 10, // Top Back Left
    TBR = 11, // Top Back Right
    // Extended — Apple Music Atmos 9.1.6 / Dolby Atmos 9.1.6
    LSS = 12, // Left Side Surround  (ch12 in Apple Music 16ch)
    RSS = 13, // Right Side Surround (ch13 in Apple Music 16ch)
    TSL = 14, // Top Side Left       (ch14)
    TSR = 15, // Top Side Right      (ch15)
    INVALID = 255
};

// SOFA azimuth/elevation for each channel (degrees)
// Convention: azimuth 0° = front, +90° = left, elevation 0° = horizontal
struct SpeakerPosition {
    float azimuth;    // degrees
    float elevation;  // degrees
    float distance;   // meters (usually 1.0 for HRTF)
};

// 7.1.4 standard positions — ITU-R BS.2051 / BS.2094 / Dolby Atmos
// azimuth: 0°=front, +90°=left, -90°=right (SOFA convention)
// Indexed by Channel enum value (0–11)
constexpr std::array<SpeakerPosition, 16> SPEAKER_POSITIONS_7_1_4 = {{
    {  30.0f,   0.0f, 1.0f },  // FL
    { -30.0f,   0.0f, 1.0f },  // FR
    {   0.0f,   0.0f, 1.0f },  // FC
    {   0.0f,   0.0f, 1.0f },  // LFE (bypass — no HRIR)
    { 135.0f,   0.0f, 1.0f },  // BL  (rear surround)
    {-135.0f,   0.0f, 1.0f },  // BR  (rear surround)
    {  90.0f,   0.0f, 1.0f },  // SL
    { -90.0f,   0.0f, 1.0f },  // SR
    {  45.0f,  45.0f, 1.0f },  // TFL (Top Front Left)
    { -45.0f,  45.0f, 1.0f },  // TFR (Top Front Right)
    { 135.0f,  45.0f, 1.0f },  // TBL (Top Back Left)
    {-135.0f,  45.0f, 1.0f },  // TBR (Top Back Right)
    {  90.0f,   0.0f, 1.0f },  // LSS (Left Side Surround  — same az as SL)
    { -90.0f,   0.0f, 1.0f },  // RSS (Right Side Surround — same az as SR)
    {  90.0f,  45.0f, 1.0f },  // TSL (Top Side Left)
    { -90.0f,  45.0f, 1.0f },  // TSR (Top Side Right)
}};

// Backwards compat alias (7.1 subset is first 8 entries)
inline constexpr auto& SPEAKER_POSITIONS_7_1 = SPEAKER_POSITIONS_7_1_4;

inline constexpr const char* channelName(Channel ch) noexcept
{
    switch (ch) {
        case Channel::FL:  return "FL";
        case Channel::FR:  return "FR";
        case Channel::FC:  return "FC";
        case Channel::LFE: return "LFE";
        case Channel::BL:  return "BL";
        case Channel::BR:  return "BR";
        case Channel::SL:  return "SL";
        case Channel::SR:  return "SR";
        case Channel::TFL: return "TFL";
        case Channel::TFR: return "TFR";
        case Channel::TBL: return "TBL";
        case Channel::TBR: return "TBR";
        case Channel::LSS: return "LSS";
        case Channel::RSS: return "RSS";
        case Channel::TSL: return "TSL";
        case Channel::TSR: return "TSR";
        default:           return "INVALID";
    }
}

struct AudioConfig {
    int inputDevice  = -1;   // PortAudio device index (-1 = default)
    int outputDevice = -1;
    int inputChannels  = 16;  // 9.1.6 Atmos bed default (Apple Music / Dolby 16ch)
    int outputChannels = 2;
    float masterGainDb = -9.5f;
    float lfeGainDb    = 0.0f;
    bool  keepAlive    = false;  // send silent stream to keep DAC awake
    bool  swapLR       = false;  // swap L/R output (for reversed IEM wear)
    bool  stereoEqMode = false;  // bypass binaural engine — stereo passthrough + EQ only
    int   bufferFrames = BUFFER_FRAMES;  // PortAudio buffer size (tweak to reduce xruns)
    std::string sofaPath;    // path to .sofa HRIR file
    std::string eqPath;      // path to APO ParametricEQ .txt (empty = no EQ)
};

} // namespace hearGOD
