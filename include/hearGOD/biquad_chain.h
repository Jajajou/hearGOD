#pragma once
#include <vector>
#include <Accelerate/Accelerate.h>

namespace hearGOD {

// Second-order IIR filter (biquad) coefficients
struct BiquadCoeff {
    float b0, b1, b2;  // feedforward
    float a1, a2;      // feedback (a0 normalized to 1.0)
};

// Filter types for convenience constructors
enum class FilterType { PEAK, LOWSHELF, HIGHSHELF, LOWPASS, HIGHPASS };

BiquadCoeff makeBiquad(FilterType type, float freqHz, float gainDb,
                       float Q, float sampleRate);

// Stereo biquad chain using vDSP biquadm (vectorized, all sections at once)
// Target curve EQ sits after HRIR convolution sum
class BiquadChain {
public:
    void setFilters(const std::vector<BiquadCoeff>& coeffs);

    // Set preamp gain (dB). Applied as linear scale after biquad processing.
    void setPreamp(float db);

    // Process stereo interleaved buffer in-place
    // data: L0 R0 L1 R1 ... (2 * numFrames floats)
    void processStereoInterleaved(float* data, int numFrames);

    bool isEmpty() const { return coeffs_.empty(); }

    ~BiquadChain();

private:
    std::vector<BiquadCoeff> coeffs_;
    float preampLinear_ = 1.0f;

    // vDSP biquadm setup
    vDSP_biquadm_Setup setup_ = nullptr;
    std::vector<double> vdspCoeffs_;  // vDSP wants double; delay state inside setup

    void rebuildSetup();
};

} // namespace hearGOD
