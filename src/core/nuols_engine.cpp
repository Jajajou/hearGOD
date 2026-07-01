#include "hearGOD/nuols_engine.h"
#include <cstring>
#include <cmath>
#include <numbers>

namespace hearGOD {

static constexpr float PI = std::numbers::pi_v<float>;

static float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

NUOLSEngine::NUOLSEngine(int partitionSize, int numPartitions)
    : partitionSize_(partitionSize)
    , numPartitions_(numPartitions)
{
    tempL_.resize(partitionSize_, 0.0f);
    tempR_.resize(partitionSize_, 0.0f);

    for (int i = 0; i < static_cast<int>(fdls_.size()); ++i) {
        fdls_[i].left  = std::make_unique<FDL>(partitionSize_, numPartitions_);
        fdls_[i].right = std::make_unique<FDL>(partitionSize_, numPartitions_);
    }

    initLfeLpf();
}

void NUOLSEngine::initLfeLpf()
{
    // 4th-order Butterworth LPF @ 125Hz / 48kHz
    // Bilinear transform, two cascaded biquads
    // Pole angles: (2k+1)*pi/(2*4), k=0,1 for upper two poles
    // Q values: 1/(2*cos(pole_angle)) → Q1=0.54120, Q2=1.30656
    float fc = 125.0f;
    float fs = static_cast<float>(SAMPLE_RATE);
    float w0 = 2.0f * PI * fc / fs;
    float cw = std::cos(w0);
    float sw = std::sin(w0);

    auto makeLp = [&](float Q) -> Biquad2 {
        float alpha = sw / (2.0f * Q);
        float a0inv = 1.0f / (1.0f + alpha);
        Biquad2 bq;
        bq.b0 = (1.0f - cw) / 2.0f * a0inv;
        bq.b1 = (1.0f - cw)         * a0inv;
        bq.b2 = (1.0f - cw) / 2.0f * a0inv;
        bq.a1 = -2.0f * cw          * a0inv;
        bq.a2 = (1.0f - alpha)      * a0inv;
        return bq;
    };

    lfeBq1_ = makeLp(0.54120f);
    lfeBq2_ = makeLp(1.30656f);
}

void NUOLSEngine::setLfeGainDb(float db)
{
    lfeGainLinear_ = dbToLinear(db);
}

void NUOLSEngine::setMasterGainDb(float db)
{
    masterGainLinear_ = dbToLinear(db);
}

void NUOLSEngine::loadHRIR(Channel ch, const float* hrirL, const float* hrirR, int hrirLength)
{
    int idx = static_cast<int>(ch);
    if (idx >= static_cast<int>(fdls_.size())) return;
    fdls_[idx].left->loadHRIR(hrirL, hrirLength);
    fdls_[idx].right->loadHRIR(hrirR, hrirLength);
}

bool NUOLSEngine::isReady() const
{
    int fl = static_cast<int>(Channel::FL);
    int fr = static_cast<int>(Channel::FR);
    return fdls_[fl].left->isLoaded()  && fdls_[fl].right->isLoaded()
        && fdls_[fr].left->isLoaded()  && fdls_[fr].right->isLoaded();
}

void NUOLSEngine::process(const float* const* in, float* out, int numChannels)
{
    std::memset(out, 0, 2 * partitionSize_ * sizeof(float));

    for (int ch = 0; ch < numChannels && ch < static_cast<int>(fdls_.size()); ++ch) {
        Channel channel = static_cast<Channel>(ch);

        if (channel == Channel::LFE) {
            const float* lfe = in[ch];
            for (int f = 0; f < partitionSize_; ++f) {
                // LPF then write into delay line; read delayed sample for output
                float filtered = lfeBq2_.tick(lfeBq1_.tick(lfe[f]));
                lfeDelayBuf_[lfeDelayWrite_] = filtered;
                lfeDelayWrite_ = (lfeDelayWrite_ + 1) % LFE_DELAY_SAMPLES;
                float s = lfeDelayBuf_[lfeDelayWrite_] * lfeGainLinear_;
                out[2 * f]     += s;
                out[2 * f + 1] += s;
            }
            continue;
        }

        auto& pair = fdls_[ch];
        if (!pair.left->isLoaded()) continue;

        pair.left->process(in[ch], tempL_.data());
        pair.right->process(in[ch], tempR_.data());

        for (int f = 0; f < partitionSize_; ++f) {
            out[2 * f]     += tempL_[f];
            out[2 * f + 1] += tempR_[f];
        }
    }

    // Master gain — one vDSP_vsmul over full interleaved buffer
    if (masterGainLinear_ != 1.0f) {
        for (int i = 0; i < 2 * partitionSize_; ++i)
            out[i] *= masterGainLinear_;
    }
}

} // namespace hearGOD
