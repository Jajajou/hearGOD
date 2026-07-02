#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "hearGOD/nuols_engine.h"
#include "hearGOD/biquad_chain.h"
#include "hearGOD/types.h"
#include <memory>
#include <atomic>

namespace hearGOD {

// JUCE audio I/O callback — mirrors PortAudio callback logic.
// Owns NUOLSEngine and BiquadChain; MainComponent constructs and holds this.
class HearGodAudioProcessor : public juce::AudioIODeviceCallback
{
public:
    explicit HearGodAudioProcessor(AudioConfig cfg);

    // Replace engine+EQ atomically (called from message thread, takes effect next callback).
    void setEngine(std::shared_ptr<NUOLSEngine> engine);
    void setEQ(std::shared_ptr<BiquadChain> eq);

    // Current config — read by UI for display.
    const AudioConfig& config() const { return config_; }

    void setMasterGainDb(float db) {
        config_.masterGainDb = db;
        if (engine_) engine_->setMasterGainDb(db);
    }
    void setStereoEqMode(bool stereo) { config_.stereoEqMode = stereo; }

    // Metrics updated each callback.
    struct Metrics {
        std::atomic<int>   xrunCount{0};
        // Per-input-channel peaks (index = Channel enum value, MAX_CHANNELS entries)
        std::array<std::atomic<float>, MAX_CHANNELS> inputPeak{};
        // Binaural output peaks
        std::atomic<float> binauralPeakL{0.0f};
        std::atomic<float> binauralPeakR{0.0f};
        Metrics() {
            for (auto& a : inputPeak) a.store(0.0f);
        }
    };
    const Metrics& metrics() const { return metrics_; }
    void resetXruns() { metrics_.xrunCount.store(0); }

    // juce::AudioIODeviceCallback
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

private:
    AudioConfig config_;

    // Shared with UI thread via atomic pointer swap — RT-safe single-producer.
    std::shared_ptr<NUOLSEngine> engine_;
    std::shared_ptr<BiquadChain> eq_;

    // Per-callback scratch buffer: interleaved stereo out, MAX_BUFFER_FRAMES*2.
    std::array<float, MAX_BUFFER_FRAMES * 2> interleavedOut_{};

    Metrics metrics_;

    float computePeak(const float* buf, int numSamples) const noexcept;
};

} // namespace hearGOD
