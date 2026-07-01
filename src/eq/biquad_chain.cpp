#include "hearGOD/biquad_chain.h"
#include "hearGOD/types.h"
#include <cstring>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace hearGOD {

static constexpr float PI = std::numbers::pi_v<float>;

BiquadCoeff makeBiquad(FilterType type, float freqHz, float gainDb,
                       float Q, float sampleRate)
{
    float A  = std::pow(10.0f, gainDb / 40.0f);  // sqrt(10^(dB/20))
    float w0 = 2.0f * PI * freqHz / sampleRate;
    float cw = std::cos(w0);
    float sw = std::sin(w0);
    float alpha = sw / (2.0f * Q);

    BiquadCoeff c{};

    switch (type) {
    case FilterType::PEAK: {
        float alphaA = alpha * A;
        float alphaOA = alpha / A;
        float a0inv = 1.0f / (1.0f + alphaOA);
        c.b0 = (1.0f + alphaA)  * a0inv;
        c.b1 = (-2.0f * cw)     * a0inv;
        c.b2 = (1.0f - alphaA)  * a0inv;
        c.a1 = (-2.0f * cw)     * a0inv;
        c.a2 = (1.0f - alphaOA) * a0inv;
        break;
    }
    case FilterType::LOWSHELF: {
        float sqA = std::sqrt(A);
        float alphaS = sw / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/Q - 1.0f) + 2.0f);
        float a0inv = 1.0f / (  (A+1) + (A-1)*cw + 2*sqA*alphaS );
        c.b0 = A * ( (A+1) - (A-1)*cw + 2*sqA*alphaS ) * a0inv;
        c.b1 = 2*A * ( (A-1) - (A+1)*cw )               * a0inv;
        c.b2 = A * ( (A+1) - (A-1)*cw - 2*sqA*alphaS ) * a0inv;
        c.a1 = -2   * ( (A-1) + (A+1)*cw )               * a0inv;
        c.a2 = (       (A+1) + (A-1)*cw - 2*sqA*alphaS ) * a0inv;
        break;
    }
    case FilterType::HIGHSHELF: {
        float sqA = std::sqrt(A);
        float alphaS = sw / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/Q - 1.0f) + 2.0f);
        float a0inv = 1.0f / ( (A+1) - (A-1)*cw + 2*sqA*alphaS );
        c.b0 = A * ( (A+1) + (A-1)*cw + 2*sqA*alphaS ) * a0inv;
        c.b1 = -2*A * ( (A-1) + (A+1)*cw )              * a0inv;
        c.b2 = A * ( (A+1) + (A-1)*cw - 2*sqA*alphaS ) * a0inv;
        c.a1 = 2    * ( (A-1) - (A+1)*cw )              * a0inv;
        c.a2 = (       (A+1) - (A-1)*cw - 2*sqA*alphaS ) * a0inv;
        break;
    }
    case FilterType::LOWPASS: {
        float a0inv = 1.0f / (1.0f + alpha);
        c.b0 = (1.0f - cw) / 2.0f * a0inv;
        c.b1 = (1.0f - cw)         * a0inv;
        c.b2 = (1.0f - cw) / 2.0f * a0inv;
        c.a1 = -2.0f * cw          * a0inv;
        c.a2 = (1.0f - alpha)      * a0inv;
        break;
    }
    case FilterType::HIGHPASS: {
        float a0inv = 1.0f / (1.0f + alpha);
        c.b0 =  (1.0f + cw) / 2.0f * a0inv;
        c.b1 = -(1.0f + cw)         * a0inv;
        c.b2 =  (1.0f + cw) / 2.0f * a0inv;
        c.a1 = -2.0f * cw           * a0inv;
        c.a2 = (1.0f - alpha)       * a0inv;
        break;
    }
    }

    return c;
}

void BiquadChain::setFilters(const std::vector<BiquadCoeff>& coeffs)
{
    coeffs_ = coeffs;
    rebuildSetup();
}

void BiquadChain::setPreamp(float db)
{
    preampLinear_ = std::pow(10.0f, db / 20.0f);
}

void BiquadChain::rebuildSetup()
{
    if (setup_) {
        vDSP_biquadm_DestroySetup(setup_);
        setup_ = nullptr;
    }
    if (coeffs_.empty()) return;

    int M = static_cast<int>(coeffs_.size());

    // vDSP_biquadm layout: M sections × N channels × 5 coeffs, section-major channel-minor.
    // [sec0_ch0×5, sec0_ch1×5, sec1_ch0×5, sec1_ch1×5, ...]
    vdspCoeffs_.resize(M * 2 * 5);
    for (int i = 0; i < M; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            int base = (i * 2 + ch) * 5;
            vdspCoeffs_[base + 0] = coeffs_[i].b0;
            vdspCoeffs_[base + 1] = coeffs_[i].b1;
            vdspCoeffs_[base + 2] = coeffs_[i].b2;
            vdspCoeffs_[base + 3] = coeffs_[i].a1;
            vdspCoeffs_[base + 4] = coeffs_[i].a2;
        }
    }

    setup_ = vDSP_biquadm_CreateSetup(vdspCoeffs_.data(), M, 2);
}

void BiquadChain::processStereoInterleaved(float* data, int numFrames)
{
    if (!setup_ || coeffs_.empty()) return;

    int N = static_cast<int>(coeffs_.size());

    // Stack buffers — no heap in RT path. MAX_BUFFER_FRAMES is compile-time upper bound.
    float L[MAX_BUFFER_FRAMES], R[MAX_BUFFER_FRAMES];

    for (int i = 0; i < numFrames; ++i) {
        L[i] = data[2 * i];
        R[i] = data[2 * i + 1];
    }

    const float* in[2]  = { L, R };
    float*       out[2] = { L, R };

    vDSP_biquadm(setup_, in, 1, out, 1, numFrames);

    if (preampLinear_ != 1.0f) {
        vDSP_vsmul(L, 1, &preampLinear_, L, 1, numFrames);
        vDSP_vsmul(R, 1, &preampLinear_, R, 1, numFrames);
    }

    for (int i = 0; i < numFrames; ++i) {
        data[2 * i]     = L[i];
        data[2 * i + 1] = R[i];
    }
}

BiquadChain::~BiquadChain()
{
    if (setup_) vDSP_biquadm_DestroySetup(setup_);
}

} // namespace hearGOD
