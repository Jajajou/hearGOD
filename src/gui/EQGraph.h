#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "hearGOD/biquad_chain.h"
#include "hearGOD/peq_parser.h"
#include "hearGOD/types.h"
#include <vector>
#include <functional>
#include <atomic>

namespace hearGOD {

// Displays EQ filter response + optional raw FR and target curve.
// Curves are normalized at a user-selectable reference frequency (default 1 kHz).
// Style inspired by PEQdB: raw FR (cyan), target (white), EQ result (purple fill).
class EQGraph : public juce::Component, private juce::Timer
{
public:
    EQGraph();
    ~EQGraph() override = default;

    // EQ filter response — call after loading a preset.
    void setPreset(const PEQPreset& preset, float sampleRate = 48000.0f);
    void clearPreset();

    // Raw headphone FR measurement: list of (freqHz, dB) points.
    // Stored raw — will be normalized at normFreq_ before display.
    void setRawFR(std::vector<std::pair<float, float>> points);
    void clearRawFR();

    // Target curve (Harman, PEQdB Diamond, etc.) — same format.
    void setTargetCurve(std::vector<std::pair<float, float>> points);
    void clearTargetCurve();

    // Normalization reference frequency (default 1000 Hz).
    // All curves shift so value at this freq = 0 dB.
    void setNormFreq(float hz);
    float normFreq() const noexcept { return normFreq_; }

    // Per-curve manual offsets (dB) — applied after normalization.
    void setRawFROffset(float db);
    float rawFROffset() const noexcept { return rawFROffset_; }

    // Audio-thread safe — lock-free ring buffer.
    void pushWaveformSamples(const float* mono, int n) noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override {}

private:
    static constexpr uint32_t kBg      = 0xFF141418;
    static constexpr uint32_t kGrid    = 0xFF2A2A35;
    static constexpr uint32_t kAccent  = 0xFF7C5CBF;
    static constexpr uint32_t kZeroDb  = 0xFF3F3F46;
    static constexpr uint32_t kText    = 0xFF71717A;
    static constexpr uint32_t kRawFR   = 0xFF22D3EE; // cyan
    static constexpr uint32_t kTarget  = 0xFFF1F5F9; // near-white
    static constexpr uint32_t kResult  = 0xFF7C5CBF; // purple

    // EQ filter magnitudes (relative dB, preamp included)
    std::vector<float> eqMag_;
    float              eqSampleRate_ = 48000.0f;
    PEQPreset          eqPreset_;
    bool               hasPreset_    = false;
    int                cachedWidth_  = 0;

    // Raw points stored as-is (not pre-normalized — normFreq_ may change later)
    std::vector<std::pair<float, float>> rawFRRaw_;
    std::vector<std::pair<float, float>> targetRaw_;

    float normFreq_    = 1000.0f;
    float rawFROffset_ = 0.0f;

    void recomputeMagnitudes(int width);

    // Normalize pts so value at refHz = 0 dB, then shift by extraOffsetDb.
    std::vector<std::pair<float, float>> normalizedCopy(
        const std::vector<std::pair<float, float>>& pts,
        float extraOffsetDb = 0.0f) const;

    static float interpolate(
        const std::vector<std::pair<float, float>>& pts, float freq) noexcept;

    // Waveform overlay — call from audio thread (lock-free ring buffer).
    // Scale: [-1, +1] amplitude maps to ±kWaveDbRange dB around 0dB line.
    static constexpr int   kWaveRingSize  = 4096; // power-of-2
    static constexpr float kWaveDbRange   = 6.0f; // ±6 dB visual excursion
    static constexpr uint32_t kWave       = 0xCCFFFFFF; // white 80%

    std::array<float, kWaveRingSize> waveRing_{};
    std::atomic<int> waveWrite_{0};  // audio thread writes
    std::atomic<int> waveCount_{0};  // samples available

    void timerCallback() override { if (waveCount_.load() > 0) repaint(); }

    float freqForX(float x, float width) const noexcept;
    float yForDb(float db, float height, float dbRange = 20.0f) const noexcept;

    juce::Path buildCurvePath(
        const std::vector<std::pair<float, float>>& pts,
        float W, float H, float dbRange) const;
};

} // namespace hearGOD
