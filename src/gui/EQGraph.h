#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include "hearGOD/biquad_chain.h"
#include "hearGOD/peq_parser.h"
#include "hearGOD/types.h"
#include <vector>
#include <functional>
#include <atomic>
#include <Accelerate/Accelerate.h>

namespace hearGOD {

// Displays EQ filter response + optional raw FR and target curve.
// Curves are normalized at a user-selectable reference frequency (default 1 kHz).
// Style inspired by PEQdB: raw FR (cyan), target (white), EQ result (purple fill).
class EQGraph : public juce::Component, private juce::Timer
{
public:
    EQGraph();
    ~EQGraph() override { if (fftSetup_) vDSP_destroy_fftsetup(fftSetup_); }

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

    // Spectrum analyzer — audio thread feeds ring, GUI thread runs FFT on timer.
    static constexpr int kFFTOrder     = 11;           // 2048-point FFT
    static constexpr int kFFTSize      = 1 << kFFTOrder;
    static constexpr int kSpecBins     = kFFTSize / 2;
    static constexpr int kWaveRingSize = kFFTSize * 4; // power-of-2, ≥ kFFTSize
    static constexpr uint32_t kSpectrum = 0x4DFFB3FF;  // teal-green

    std::array<float, kWaveRingSize> waveRing_{};
    std::atomic<int>  waveWrite_{0};
    std::atomic<bool> waveReady_{false};

    // FFT scratch (GUI thread only — no locking needed)
    std::array<float, kFFTSize>   fftWindow_{};    // Hann window coefficients
    std::array<float, kFFTSize>   fftBuf_{};
    DSPSplitComplex               fftSplit_{};
    std::array<float, kSpecBins>  fftReal_{};
    std::array<float, kSpecBins>  fftImag_{};
    std::array<float, kSpecBins>  specSmooth_{};   // smoothed magnitude dB
    FFTSetup                      fftSetup_{};

    void initFFT();
    void timerCallback() override;
    void runSpectrum();                            // called from timerCallback

    float freqForX(float x, float width) const noexcept;
    float yForDb(float db, float height, float dbRange = 20.0f) const noexcept;

    juce::Path buildCurvePath(
        const std::vector<std::pair<float, float>>& pts,
        float W, float H, float dbRange) const;
};

} // namespace hearGOD
