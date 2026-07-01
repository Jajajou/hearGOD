#pragma once
#include <vector>
#include <complex>
#include <Accelerate/Accelerate.h>

namespace hearGOD {

// Frequency Delay Line — one FDL per (channel, ear) pair
// Implements uniform partitioned OLS using Apple vDSP
class FDL {
public:
    // fftSize: must be power-of-2, = 2 * partitionSize
    // numPartitions: HRIR_length / partitionSize (rounded up)
    FDL(int partitionSize, int numPartitions);
    ~FDL();

    // Load pre-computed HRIR partition FFTs
    // hrirSamples: time-domain HRIR, length = partitionSize * numPartitions
    void loadHRIR(const float* hrirSamples, int hrirLength);

    // Process one input block (partitionSize samples)
    // Accumulates into outputAccum (fftSize samples, time-domain after IFFT)
    void process(const float* inputBlock, float* outputTime);

    bool isLoaded() const { return loaded_; }

private:
    int partitionSize_;
    int fftSize_;        // = 2 * partitionSize
    int numPartitions_;

    // vDSP FFT setup
    FFTSetup fftSetup_;
    int log2FftSize_;

    // Ring buffer of FFT'd input blocks — one DSPSplitComplex per partition
    // realp/imagp each point to a heap array of fftSize/2+1 floats
    std::vector<DSPSplitComplex> inputRing_;
    int writeIdx_ = 0;

    // Pre-computed HRIR partition FFTs (same layout)
    std::vector<DSPSplitComplex> hrirFFT_;

    // Scratch buffers
    std::vector<float> realBuf_, imagBuf_;
    std::vector<float> accumReal_, accumImag_;
    std::vector<float> overlapBuf_;  // OLS overlap region

    bool loaded_ = false;

    void allocSplitComplex(DSPSplitComplex& sc, int size);
    void freeSplitComplex(DSPSplitComplex& sc);
};

} // namespace hearGOD
