#pragma once
#include <vector>
#include <complex>
#include <atomic>
#include <Accelerate/Accelerate.h>

namespace hearGOD {

// Frequency Delay Line — one FDL per (channel, ear) pair
// Implements uniform partitioned OLS using Apple vDSP
// Supports click-free HRIR hot-swap: control thread stages a new HRIR into
// spectra set B (loadHRIRPending), audio thread crossfades A→B over
// crossfadeBlocks_ blocks (both convolved on the shared input ring).
class FDL {
public:
    // fftSize: must be power-of-2, = 2 * partitionSize
    // numPartitions: HRIR_length / partitionSize (rounded up)
    // crossfadeBlocks: blocks over which A→B crossfade ramps (>=1)
    FDL(int partitionSize, int numPartitions, int crossfadeBlocks = 10);
    ~FDL();

    // Load pre-computed HRIR partition FFTs (immediate, control thread, stream stopped)
    // hrirSamples: time-domain HRIR, length = partitionSize * numPartitions
    void loadHRIR(const float* hrirSamples, int hrirLength);

    // Stage a new HRIR for click-free crossfade while stream runs.
    // Control thread only. Returns false if a previous swap is still in flight.
    bool loadHRIRPending(const float* hrirSamples, int hrirLength);

    bool isCrossfading() const { return state_.load(std::memory_order_acquire) != State::Idle; }

    // Process one input block (partitionSize samples)
    // Accumulates into outputAccum (fftSize samples, time-domain after IFFT)
    void process(const float* inputBlock, float* outputTime);

    bool isLoaded() const { return loaded_; }

private:
    enum class State : int { Idle = 0, Pending = 1, Fading = 2 };

    int partitionSize_;
    int fftSize_;        // = 2 * partitionSize
    int numPartitions_;
    int crossfadeBlocks_;

    // vDSP FFT setup
    FFTSetup fftSetup_;
    int log2FftSize_;

    // Ring buffer of FFT'd input blocks — one DSPSplitComplex per partition
    // realp/imagp each point to a heap array of fftSize/2+1 floats
    std::vector<DSPSplitComplex> inputRing_;
    int writeIdx_ = 0;

    // Pre-computed HRIR partition FFTs. A = active, B = staged (same layout).
    std::vector<DSPSplitComplex> hrirFFT_A_;
    std::vector<DSPSplitComplex> hrirFFT_B_;

    std::atomic<State> state_{State::Idle};
    int fadeBlock_ = 0;  // audio-thread only

    // Scratch buffers
    std::vector<float> realBuf_, imagBuf_;
    std::vector<float> accumReal_, accumImag_;
    std::vector<float> timeB_;       // time-domain result of B during fade
    std::vector<float> overlapBuf_;   // OLS overlap region (A)
    std::vector<float> overlapBufB_;  // OLS overlap region (B during fade)

    bool loaded_ = false;

    void allocSplitComplex(DSPSplitComplex& sc, int size);
    void freeSplitComplex(DSPSplitComplex& sc);
    void fftHRIRInto(std::vector<DSPSplitComplex>& dst, const float* hrirSamples, int hrirLength);
    // Convolve input ring with given spectra set → time-domain fftSize_ samples in realBuf_
    void convolve(const std::vector<DSPSplitComplex>& spectra);
};

} // namespace hearGOD
