#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "hearGOD/types.h"
#include <array>
#include <atomic>

namespace hearGOD {

// Right-panel meter panel matching the HTML prototype:
//   - "LEVELS" section: one horizontal bar per active input channel, with tag + dB readout
//   - "BINAURAL OUT" section: L and R horizontal bars
class MultiChannelMeter : public juce::Component, private juce::Timer
{
public:
    MultiChannelMeter();
    ~MultiChannelMeter() override;

    // Set which input channels are active (determines which rows to show)
    void setActiveChannels(int numChannels);

    // Update peaks — call from any thread
    void setInputPeak(int channelIndex, float linear);
    void setBinauralPeak(float peakL, float peakR);

    void paint(juce::Graphics& g) override;
    void resized() override {}

private:
    void timerCallback() override;

    int numChannels_ = 16;

    struct Chan {
        std::atomic<float> peak{0.0f};
        float display  = 0.0f;
        float hold     = 0.0f;
        int   holdCount = 0;
    };

    std::array<Chan, MAX_CHANNELS> inputs_;
    Chan binauralL_;
    Chan binauralR_;

    static constexpr int   kHoldFrames    = 30;
    static constexpr float kDecayPerFrame = 0.88f;
    static constexpr float kBarH          = 14.0f;
    static constexpr float kGap           = 3.0f;
    static constexpr float kTagW          = 30.0f;
    static constexpr float kDbW           = 38.0f;
    static constexpr float kSectionGap    = 12.0f;

    void updateChannel(Chan& ch);
    void drawBar(juce::Graphics& g, float x, float y, float w,
                 const Chan& ch, bool isBinaural = false) const;
    float linearToNorm(float linear) const noexcept;
    juce::String dbText(float linear) const;

    static const char* channelTag(int idx);
};

} // namespace hearGOD
