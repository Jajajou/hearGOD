#include "hearGOD/dfe_compensator.h"
#include <Accelerate/Accelerate.h>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <numbers>
#include <numeric>

namespace hearGOD {

static constexpr float PI = std::numbers::pi_v<float>;

// ---- vDSP RAII helpers ---------------------------------------------------

struct FFTSetupGuard {
    vDSP_DFT_Setup setup;
    explicit FFTSetupGuard(int n)
        : setup(vDSP_DFT_zop_CreateSetup(nullptr, n, vDSP_DFT_FORWARD))
    {
        if (!setup) throw std::runtime_error("vDSP_DFT_zop_CreateSetup failed");
    }
    ~FFTSetupGuard() { vDSP_DFT_DestroySetup(setup); }
};

struct IFFTSetupGuard {
    vDSP_DFT_Setup setup;
    explicit IFFTSetupGuard(int n)
        : setup(vDSP_DFT_zop_CreateSetup(nullptr, n, vDSP_DFT_INVERSE))
    {
        if (!setup) throw std::runtime_error("vDSP_DFT_zop_CreateSetup failed");
    }
    ~IFFTSetupGuard() { vDSP_DFT_DestroySetup(setup); }
};

// Forward DFT: real input → complex output (interleaved re,im pairs)
static void dft(vDSP_DFT_Setup setup, const std::vector<float>& x,
                std::vector<float>& re, std::vector<float>& im, int n)
{
    re.assign(n, 0.0f);
    im.assign(n, 0.0f);
    // zero-pad input into re[]
    int len = std::min((int)x.size(), n);
    std::copy(x.begin(), x.begin() + len, re.begin());
    vDSP_DFT_Execute(setup, re.data(), im.data(), re.data(), im.data());
}

// Inverse DFT + scale by 1/n
static void idft(vDSP_DFT_Setup setup, const std::vector<float>& re,
                 const std::vector<float>& im,
                 std::vector<float>& outRe, std::vector<float>& outIm, int n)
{
    outRe = re; outIm = im;
    vDSP_DFT_Execute(setup, outRe.data(), outIm.data(), outRe.data(), outIm.data());
    float scale = 1.0f / n;
    vDSP_vsmul(outRe.data(), 1, &scale, outRe.data(), 1, n);
    vDSP_vsmul(outIm.data(), 1, &scale, outIm.data(), 1, n);
}

// ---- DFECompensator::compute ---------------------------------------------

void DFECompensator::compute(const std::vector<std::vector<float>>& hrirs,
                              int irLength, int fftSize)
{
    if (hrirs.empty() || irLength <= 0) return;

    // Choose FFT size: >= 2*irLength, power of 2
    if (fftSize <= 0) {
        fftSize = 1;
        while (fftSize < 2 * irLength) fftSize <<= 1;
    }

    FFTSetupGuard fwdSetup(fftSize);
    IFFTSetupGuard invSetup(fftSize);

    // Accumulate average power spectrum (L2 average across all IRs)
    std::vector<double> avgMag(fftSize, 0.0);
    int count = 0;

    std::vector<float> re, im;
    for (const auto& hrir : hrirs) {
        dft(fwdSetup.setup, hrir, re, im, fftSize);
        for (int k = 0; k < fftSize; ++k)
            avgMag[k] += std::sqrt((double)re[k]*re[k] + (double)im[k]*im[k]);
        ++count;
    }

    // Normalize to average magnitude
    std::vector<float> magSpec(fftSize);
    for (int k = 0; k < fftSize; ++k)
        magSpec[k] = static_cast<float>(avgMag[k] / count);

    // Minimum-phase inverse filter from average magnitude
    invFilter_ = minimumPhaseFromMagnitude(magSpec, fftSize);

    // Truncate to irLength (causal head of inverse filter)
    invFilter_.resize(irLength);

    ready_ = true;
    std::cout << "[DFE] Computed from " << count << " HRIRs, FFT=" << fftSize
              << ", filter length=" << irLength << "\n";
}

// ---- DFECompensator::apply -----------------------------------------------

std::vector<float> DFECompensator::apply(const float* hrir, int irLength) const
{
    if (!ready_) return std::vector<float>(hrir, hrir + irLength);

    // Linear convolution length = irLength + invFilter_.size() - 1
    // We truncate back to irLength (discard tail — it's small, invFilter is short)
    int convLen = irLength + static_cast<int>(invFilter_.size()) - 1;
    int fftSize = 1;
    while (fftSize < convLen) fftSize <<= 1;

    FFTSetupGuard fwdSetup(fftSize);
    IFFTSetupGuard invSetup(fftSize);

    // FFT of HRIR
    std::vector<float> hVec(hrir, hrir + irLength);
    std::vector<float> hRe, hIm;
    dft(fwdSetup.setup, hVec, hRe, hIm, fftSize);

    // FFT of inverse filter
    std::vector<float> iRe, iIm;
    dft(fwdSetup.setup, invFilter_, iRe, iIm, fftSize);

    // Complex multiply
    std::vector<float> prodRe(fftSize), prodIm(fftSize);
    for (int k = 0; k < fftSize; ++k) {
        prodRe[k] = hRe[k]*iRe[k] - hIm[k]*iIm[k];
        prodIm[k] = hRe[k]*iIm[k] + hIm[k]*iRe[k];
    }

    // IFFT
    std::vector<float> outRe, outIm;
    idft(invSetup.setup, prodRe, prodIm, outRe, outIm, fftSize);

    // Truncate to irLength
    outRe.resize(irLength);

    // Preserve RMS level of original HRIR — DFE should only change spectral shape
    float rmsIn = 0.0f, rmsOut = 0.0f;
    for (int i = 0; i < irLength; ++i) rmsIn  += hrir[i]   * hrir[i];
    for (int i = 0; i < irLength; ++i) rmsOut += outRe[i]  * outRe[i];
    if (rmsOut > 1e-12f) {
        float scale = std::sqrt(rmsIn / rmsOut);
        for (auto& s : outRe) s *= scale;
    }

    return outRe;
}

// ---- Minimum-phase reconstruction via complex cepstrum -------------------

std::vector<float> DFECompensator::minimumPhaseFromMagnitude(
    const std::vector<float>& magSpectrum, int fftSize)
{
    // Inverse filter magnitude = 1/avgMag, regularized to avoid division by near-zero
    constexpr float kFloor = 1e-4f;
    std::vector<float> invMag(fftSize);
    for (int k = 0; k < fftSize; ++k)
        invMag[k] = 1.0f / std::max(magSpectrum[k], kFloor);

    // Log magnitude → cepstrum via IFFT
    IFFTSetupGuard invSetup(fftSize);
    FFTSetupGuard  fwdSetup(fftSize);

    std::vector<float> logMag(fftSize);
    for (int k = 0; k < fftSize; ++k)
        logMag[k] = std::log(invMag[k]);

    // IFFT of log magnitude (imaginary part = 0) → real cepstrum
    std::vector<float> cepRe, cepIm;
    idft(invSetup.setup, logMag, std::vector<float>(fftSize, 0.0f), cepRe, cepIm, fftSize);

    // Causal window: keep sample 0, double samples 1..N/2-1, zero samples N/2+1..N-1
    // This converts real cepstrum → minimum-phase cepstrum
    std::vector<float> winCep(fftSize, 0.0f);
    winCep[0] = cepRe[0];
    for (int n = 1; n < fftSize / 2; ++n)
        winCep[n] = 2.0f * cepRe[n];
    winCep[fftSize / 2] = cepRe[fftSize / 2];

    // FFT → exp → complex spectrum of minimum-phase inverse filter
    std::vector<float> specRe, specIm;
    dft(fwdSetup.setup, winCep, specRe, specIm, fftSize);

    // exp(complex) = exp(re) * (cos(im) + i*sin(im))
    std::vector<float> mpRe(fftSize), mpIm(fftSize);
    for (int k = 0; k < fftSize; ++k) {
        float mag = std::exp(specRe[k]);
        mpRe[k] = mag * std::cos(specIm[k]);
        mpIm[k] = mag * std::sin(specIm[k]);
    }

    // IFFT → time-domain minimum-phase inverse filter
    std::vector<float> outRe, outIm;
    idft(invSetup.setup, mpRe, mpIm, outRe, outIm, fftSize);

    return outRe;
}

} // namespace hearGOD
