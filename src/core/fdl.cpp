#include "hearGOD/fdl.h"
#include <cstring>
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace hearGOD {

FDL::FDL(int partitionSize, int numPartitions)
    : partitionSize_(partitionSize)
    , fftSize_(2 * partitionSize)
    , numPartitions_(numPartitions)
{
    // Compute log2(fftSize) for vDSP
    log2FftSize_ = static_cast<int>(std::log2(fftSize_));
    assert((1 << log2FftSize_) == fftSize_ && "fftSize must be power of 2");

    fftSetup_ = vDSP_create_fftsetup(log2FftSize_, FFT_RADIX2);
    if (!fftSetup_)
        throw std::runtime_error("vDSP_create_fftsetup failed");

    int halfSize = fftSize_ / 2 + 1;  // Nyquist-inclusive complex bins

    // Input ring: numPartitions slots of split-complex FFT data
    inputRing_.resize(numPartitions_);
    for (auto& sc : inputRing_)
        allocSplitComplex(sc, halfSize);

    // HRIR partition FFTs
    hrirFFT_.resize(numPartitions_);
    for (auto& sc : hrirFFT_)
        allocSplitComplex(sc, halfSize);

    // Scratch
    realBuf_.assign(fftSize_, 0.0f);
    imagBuf_.assign(fftSize_, 0.0f);
    accumReal_.assign(halfSize, 0.0f);
    accumImag_.assign(halfSize, 0.0f);
    overlapBuf_.assign(partitionSize_, 0.0f);  // OLS overlap (second half)
}

FDL::~FDL()
{
    int halfSize = fftSize_ / 2 + 1;
    for (auto& sc : inputRing_)  freeSplitComplex(sc);
    for (auto& sc : hrirFFT_)    freeSplitComplex(sc);
    if (fftSetup_) vDSP_destroy_fftsetup(fftSetup_);
}

void FDL::allocSplitComplex(DSPSplitComplex& sc, int size)
{
    sc.realp = new float[size]();
    sc.imagp = new float[size]();
}

void FDL::freeSplitComplex(DSPSplitComplex& sc)
{
    delete[] sc.realp;
    delete[] sc.imagp;
    sc.realp = nullptr;
    sc.imagp = nullptr;
}

void FDL::loadHRIR(const float* hrirSamples, int hrirLength)
{
    for (int p = 0; p < numPartitions_; ++p) {
        // Zero-pad this partition into fftSize scratch buffer
        realBuf_.assign(fftSize_, 0.0f);
        int offset  = p * partitionSize_;
        int copyLen = std::min(partitionSize_, hrirLength - offset);
        if (copyLen > 0)
            std::memcpy(realBuf_.data(), hrirSamples + offset, copyLen * sizeof(float));

        // ctoz: pack interleaved real[] into split-complex (imagp ignored — FFT zeros it)
        DSPSplitComplex target { hrirFFT_[p].realp, hrirFFT_[p].imagp };
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(realBuf_.data()),
                  2, &target, 1, fftSize_ / 2);

        // Real FFT forward — vDSP_fft_zrip packs Nyquist into imagp[0]
        vDSP_fft_zrip(fftSetup_, &target, 1, log2FftSize_, FFT_FORWARD);
    }

    loaded_ = true;
}

void FDL::process(const float* inputBlock, float* outputTime)
{
    int halfSize = fftSize_ / 2 + 1;

    // --- FFT current input block ---
    // Zero-pad input to fftSize (second half = 0 for OLS)
    realBuf_.assign(fftSize_, 0.0f);
    std::memcpy(realBuf_.data(), inputBlock, partitionSize_ * sizeof(float));

    DSPSplitComplex& slot = inputRing_[writeIdx_];
    vDSP_ctoz(reinterpret_cast<DSPComplex*>(realBuf_.data()),
              2, &slot, 1, fftSize_ / 2);
    vDSP_fft_zrip(fftSetup_, &slot, 1, log2FftSize_, FFT_FORWARD);

    // --- Accumulate: FDL convolution (frequency-domain multiply-accumulate) ---
    accumReal_.assign(halfSize, 0.0f);
    accumImag_.assign(halfSize, 0.0f);
    DSPSplitComplex accum { accumReal_.data(), accumImag_.data() };

    // Bins 1..N/2-1: standard complex multiply-accumulate
    int innerBins = fftSize_ / 2 - 1;
    DSPSplitComplex accum1 { accumReal_.data() + 1, accumImag_.data() + 1 };

    for (int p = 0; p < numPartitions_; ++p) {
        int ringIdx = (writeIdx_ - p + numPartitions_) % numPartitions_;
        DSPSplitComplex& inSlot = inputRing_[ringIdx];
        DSPSplitComplex& h      = hrirFFT_[p];

        // Bin 0: DC (realp[0]) and Nyquist (imagp[0]) are both real scalars —
        // must NOT use vDSP_zvma here, which would cross-multiply them as a complex pair.
        accumReal_[0] += inSlot.realp[0] * h.realp[0];
        accumImag_[0] += inSlot.imagp[0] * h.imagp[0];

        // Bins 1..N/2-1: true complex pairs → safe to use vDSP_zvma
        DSPSplitComplex inSlot1 { inSlot.realp + 1, inSlot.imagp + 1 };
        DSPSplitComplex h1      { h.realp + 1,      h.imagp + 1      };
        vDSP_zvma(&inSlot1, 1, &h1, 1, &accum1, 1, &accum1, 1, innerBins);
    }

    // Advance write index
    writeIdx_ = (writeIdx_ + 1) % numPartitions_;

    // --- IFFT ---
    vDSP_fft_zrip(fftSetup_, &accum, 1, log2FftSize_, FFT_INVERSE);
    vDSP_ztoc(&accum, 1,
              reinterpret_cast<DSPComplex*>(realBuf_.data()),
              2, fftSize_ / 2);

    // vDSP scale: forward FFT gives 2×DFT; two forward FFTs in product → 4× before IFFT.
    // IFFT adds factor N. Total raw output = 4N × correct → divide by 4N.
    float scale = 1.0f / static_cast<float>(fftSize_ * 4);
    vDSP_vsmul(realBuf_.data(), 1, &scale, realBuf_.data(), 1, fftSize_);

    // --- OLS overlap-add ---
    // First partitionSize samples = output; add saved overlap
    vDSP_vadd(realBuf_.data(), 1, overlapBuf_.data(), 1, outputTime, 1, partitionSize_);

    // Save second half as next overlap
    std::memcpy(overlapBuf_.data(),
                realBuf_.data() + partitionSize_,
                partitionSize_ * sizeof(float));
}

} // namespace hearGOD
