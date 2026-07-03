#include "HearGodAudioProcessor.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace hearGOD {

HearGodAudioProcessor::HearGodAudioProcessor(AudioConfig cfg)
    : config_(std::move(cfg))
    , engine_(std::make_shared<NUOLSEngine>(config_.bufferFrames, 4))
    , eq_(std::make_shared<BiquadChain>())
{}

void HearGodAudioProcessor::setEngine(std::shared_ptr<NUOLSEngine> engine)
{
    // Message-thread call. std::shared_ptr assignment is not RT-safe but
    // we accept one glitch cycle vs. the complexity of lock-free pointer swap.
    engine_ = std::move(engine);
}

void HearGodAudioProcessor::setEQ(std::shared_ptr<BiquadChain> eq)
{
    eq_ = std::move(eq);
}

void HearGodAudioProcessor::audioDeviceAboutToStart(juce::AudioIODevice* /*device*/)
{
    for (auto& a : metrics_.inputPeak) a.store(0.0f);
    metrics_.binauralPeakL.store(0.0f);
    metrics_.binauralPeakR.store(0.0f);
}
void HearGodAudioProcessor::audioDeviceStopped() {}

void HearGodAudioProcessor::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    // JUCE 8 AudioIODeviceCallbackContext only carries hostTimeNs; no xrun flag.
    // Xruns are detected by monitoring audio device errors via a separate listener.
    (void)context;

    const int frames = std::min(numSamples, MAX_BUFFER_FRAMES);
    float* out = interleavedOut_.data();

    if (config_.stereoEqMode) {
        // Stereo passthrough: deinterleave planar L/R → interleaved
        const float* inL = (numInputChannels > 0) ? inputChannelData[0] : nullptr;
        const float* inR = (numInputChannels > 1) ? inputChannelData[1] : nullptr;
        for (int f = 0; f < frames; ++f) {
            out[2 * f]     = inL ? inL[f] : 0.0f;
            out[2 * f + 1] = inR ? inR[f] : 0.0f;
        }
    } else {
        const int ch = std::min(numInputChannels, MAX_CHANNELS);
        engine_->process(inputChannelData, out, ch);
    }

    if (!eq_->isEmpty())
        eq_->processStereoInterleaved(out, frames);

    if (config_.swapLR) {
        for (int f = 0; f < frames; ++f)
            std::swap(out[2 * f], out[2 * f + 1]);
    }

    // Per-input-channel peaks (before binaural processing)
    {
        const int ch = std::min(numInputChannels, MAX_CHANNELS);
        for (int c = 0; c < ch; ++c) {
            if (inputChannelData[c])
                metrics_.inputPeak[c].store(
                    computePeak(inputChannelData[c], frames),
                    std::memory_order_relaxed);
        }
    }

    // De-interleave to JUCE planar output
    if (numOutputChannels >= 2) {
        float* outL = outputChannelData[0];
        float* outR = outputChannelData[1];
        for (int f = 0; f < frames; ++f) {
            outL[f] = out[2 * f];
            outR[f] = out[2 * f + 1];
        }
        // Zero any extra output channels
        for (int c = 2; c < numOutputChannels; ++c)
            std::memset(outputChannelData[c], 0, sizeof(float) * (size_t)frames);

        metrics_.binauralPeakL.store(computePeak(outL, frames), std::memory_order_relaxed);
        metrics_.binauralPeakR.store(computePeak(outR, frames), std::memory_order_relaxed);

        // Feed waveform visualizer with mid (L+R)/2.
        if (auto* cb = waveformSink_.load(std::memory_order_acquire)) {
            for (int f = 0; f < frames; ++f)
                waveMonoBuf_[f] = (outL[f] + outR[f]) * 0.5f;
            cb(waveMonoBuf_.data(), frames);
        }
    }
}

float HearGodAudioProcessor::computePeak(const float* buf, int n) const noexcept
{
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max(peak, std::abs(buf[i]));
    return peak;
}

} // namespace hearGOD
