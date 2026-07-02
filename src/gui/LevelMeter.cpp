#include "LevelMeter.h"
#include <cmath>
#include <algorithm>

namespace hearGOD {

LevelMeter::LevelMeter()
{
    startTimerHz(50);
}

LevelMeter::~LevelMeter()
{
    stopTimer();
}

void LevelMeter::setPeaks(float l, float r)
{
    peakL_.store(l, std::memory_order_relaxed);
    peakR_.store(r, std::memory_order_relaxed);
}

void LevelMeter::timerCallback()
{
    const float newL = peakL_.load(std::memory_order_relaxed);
    const float newR = peakR_.load(std::memory_order_relaxed);

    auto updateChannel = [](float incoming, float& display, float& hold, int& holdCount) {
        if (incoming >= display) {
            display = incoming;
        } else {
            display *= kDecayPerFrame;
        }
        if (incoming >= hold) {
            hold = incoming;
            holdCount = kHoldFrames;
        } else if (holdCount > 0) {
            --holdCount;
        } else {
            hold *= kDecayPerFrame;
        }
    };

    updateChannel(newL, displayL_, holdL_, holdCountL_);
    updateChannel(newR, displayR_, holdR_, holdCountR_);
    repaint();
}

float LevelMeter::linearToY(float linear, float height) const noexcept
{
    if (linear <= 0.0f) return height;
    // Map 0 dBFS (linear=1) → top, -60 dBFS → bottom
    constexpr float kMinDb = -60.0f;
    const float db = 20.0f * std::log10(linear);
    const float t = std::clamp((db - kMinDb) / -kMinDb, 0.0f, 1.0f);
    return height * (1.0f - t);
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float W = b.getWidth();
    const float H = b.getHeight();

    g.fillAll(juce::Colour(0xFF141418));

    const float barW = (W - 6.0f) * 0.5f;
    const float gapX = 6.0f;

    struct Chan { float display; float hold; float x; };
    for (const Chan& ch : { Chan{displayL_, holdL_, 0.0f},
                             Chan{displayR_, holdR_, barW + gapX} }) {
        const float fillY = linearToY(ch.display, H);

        // Bar — green to amber to red gradient
        juce::ColourGradient grad(
            juce::Colour(0xFF22C55E), ch.x, H,
            juce::Colour(0xFFEF4444), ch.x, 0.0f,
            false);
        grad.addColour(0.75, juce::Colour(0xFFF59E0B));
        g.setGradientFill(grad);
        g.fillRect(ch.x, fillY, barW, H - fillY);

        // Background (above fill)
        g.setColour(juce::Colour(0xFF1C1C23));
        g.fillRect(ch.x, 0.0f, barW, fillY);

        // Hold line
        if (ch.hold > 0.001f) {
            const float holdY = linearToY(ch.hold, H);
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.fillRect(ch.x, holdY, barW, 1.5f);
        }

        // Clip indicator — top 3px red if over 0 dBFS
        if (ch.display >= 1.0f) {
            g.setColour(juce::Colour(0xFFEF4444));
            g.fillRect(ch.x, 0.0f, barW, 3.0f);
        }

        // Border
        g.setColour(juce::Colour(0xFF2A2A35));
        g.drawRect(ch.x, 0.0f, barW, H, 1.0f);
    }

    // dB labels on right edge
    g.setColour(juce::Colour(0xFF71717A));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f }) {
        const float linear = (db <= -60.0f) ? 0.0f : std::pow(10.0f, db / 20.0f);
        const float y = linearToY(linear, H);
        g.drawText(juce::String((int)db),
                   (int)(W - 18), (int)(y - 5), 18, 10,
                   juce::Justification::right, false);
    }
}

} // namespace hearGOD
