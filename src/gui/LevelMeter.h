#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hearGOD {

// Vertical peak-hold level meter, L+R pair side by side.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter();
    ~LevelMeter() override;

    // Call from any thread — thread-safe via atomic.
    void setPeaks(float peakL, float peakR);

    void paint(juce::Graphics& g) override;
    void resized() override {}

private:
    void timerCallback() override;

    std::atomic<float> peakL_{0.0f};
    std::atomic<float> peakR_{0.0f};

    // Display values — only touched on timer/paint thread.
    float displayL_ = 0.0f;
    float displayR_ = 0.0f;
    float holdL_    = 0.0f;
    float holdR_    = 0.0f;
    int   holdCountL_ = 0;
    int   holdCountR_ = 0;

    static constexpr int kHoldFrames = 30; // ~600ms at 50Hz timer
    static constexpr float kDecayPerFrame = 0.85f;

    float linearToY(float linear, float height) const noexcept;
};

} // namespace hearGOD
