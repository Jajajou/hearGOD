#pragma once
#include "hearGOD/types.h"
#include <vector>

namespace hearGOD {

// Diffuse-Field Equalization compensator.
//
// Computes the average magnitude response across all HRIR measurements
// in a dataset (both ears, all positions), then derives the minimum-phase
// inverse filter via cepstral liftering.
//
// Apply: convolve each HRIR with the inverse filter before loading into FDL.
// Result: HRTF spatial cues (ITD/ILD/direction-dependent coloration) preserved,
// average pinna/torso coloration removed. IEM EQ then operates independently.
//
// Usage:
//   DFECompensator dfe;
//   dfe.compute(allHrirs, irLength);          // pass all L+R IRs flat
//   auto compensated = dfe.apply(hrirL, len); // per HRIR before loadHRIR()

class DFECompensator {
public:
    // hrirs: flat list of all HRIR vectors (L and R of every measurement).
    // irLength: length of each IR in samples.
    // fftSize: FFT size for spectral work (must be >= 2*irLength, power of 2).
    void compute(const std::vector<std::vector<float>>& hrirs, int irLength,
                 int fftSize = 0);

    // Convolve a single HRIR with the pre-computed inverse filter.
    // Returns compensated HRIR of same length as input (truncated to irLength).
    std::vector<float> apply(const float* hrir, int irLength) const;

    bool isReady() const { return ready_; }

    // For diagnostics / tests: the inverse filter in time domain.
    const std::vector<float>& inverseFilter() const { return invFilter_; }

private:
    std::vector<float> invFilter_;
    bool ready_ = false;

    // Minimum-phase reconstruction via complex cepstrum:
    // 1. log magnitude spectrum
    // 2. IFFT → cepstrum
    // 3. Window (causal half)
    // 4. FFT → complex spectrum → exp → IFFT → minimum-phase IR
    static std::vector<float> minimumPhaseFromMagnitude(
        const std::vector<float>& magSpectrum, int fftSize);
};

} // namespace hearGOD
