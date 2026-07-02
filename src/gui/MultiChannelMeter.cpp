#include "MultiChannelMeter.h"
#include <cmath>
#include <algorithm>

namespace hearGOD {

static constexpr uint32_t kBg       = 0xFF141418;
static constexpr uint32_t kBorder   = 0xFF2A2A35;
static constexpr uint32_t kText     = 0xFFE4E4E7;
static constexpr uint32_t kMuted    = 0xFF71717A;
static constexpr uint32_t kGreen    = 0xFF22C55E;
static constexpr uint32_t kAmber    = 0xFFF59E0B;
static constexpr uint32_t kRed      = 0xFFEF4444;
static constexpr uint32_t kAccent   = 0xFF7C5CBF;
static constexpr uint32_t kLFE      = 0xFF8B5CF6; // purple for LFE

// Channel short-name tags matching prototype
const char* MultiChannelMeter::channelTag(int idx)
{
    switch (idx) {
        case 0:  return "L";
        case 1:  return "R";
        case 2:  return "C";
        case 3:  return "LFE";
        case 4:  return "Ls";
        case 5:  return "Rs";
        case 6:  return "SL";
        case 7:  return "SR";
        case 8:  return "TFL";
        case 9:  return "TFR";
        case 10: return "TBL";
        case 11: return "TBR";
        case 12: return "Lss";
        case 13: return "Rss";
        case 14: return "TSL";
        case 15: return "TSR";
        default: return "?";
    }
}

MultiChannelMeter::MultiChannelMeter()
{
    startTimerHz(50);
}

MultiChannelMeter::~MultiChannelMeter()
{
    stopTimer();
}

void MultiChannelMeter::setActiveChannels(int n)
{
    numChannels_ = std::clamp(n, 1, MAX_CHANNELS);
    repaint();
}

void MultiChannelMeter::setInputPeak(int ch, float linear)
{
    if (ch >= 0 && ch < MAX_CHANNELS)
        inputs_[ch].peak.store(linear, std::memory_order_relaxed);
}

void MultiChannelMeter::setBinauralPeak(float l, float r)
{
    binauralL_.peak.store(l, std::memory_order_relaxed);
    binauralR_.peak.store(r, std::memory_order_relaxed);
}

void MultiChannelMeter::updateChannel(Chan& ch)
{
    const float incoming = ch.peak.load(std::memory_order_relaxed);
    ch.display = (incoming >= ch.display) ? incoming : ch.display * kDecayPerFrame;
    if (incoming >= ch.hold) {
        ch.hold = incoming;
        ch.holdCount = kHoldFrames;
    } else if (ch.holdCount > 0) {
        --ch.holdCount;
    } else {
        ch.hold *= kDecayPerFrame;
    }
}

void MultiChannelMeter::timerCallback()
{
    for (int i = 0; i < numChannels_; ++i)
        updateChannel(inputs_[i]);
    updateChannel(binauralL_);
    updateChannel(binauralR_);
    repaint();
}

float MultiChannelMeter::linearToNorm(float linear) const noexcept
{
    if (linear <= 0.0f) return 0.0f;
    constexpr float kMinDb = -60.0f;
    const float db = 20.0f * std::log10(linear);
    return std::clamp((db - kMinDb) / -kMinDb, 0.0f, 1.0f);
}

juce::String MultiChannelMeter::dbText(float linear) const
{
    if (linear <= 0.0001f) return "-inf";
    const float db = 20.0f * std::log10(linear);
    return juce::String(db, 1);
}

void MultiChannelMeter::drawBar(juce::Graphics& g, float x, float y, float w,
                                 const Chan& ch, bool isBinaural) const
{
    // Background
    g.setColour(juce::Colour(0xFF1C1C23));
    g.fillRect(x, y, w, kBarH);

    // Filled portion
    const float fillNorm = linearToNorm(ch.display);
    const float fillW = fillNorm * w;

    if (fillW > 0.0f) {
        juce::ColourGradient grad(
            isBinaural ? juce::Colour(kGreen)  : juce::Colour(kGreen),  x,         y,
            isBinaural ? juce::Colour(kRed)    : juce::Colour(kAmber),  x + w, y,
            false);
        grad.addColour(0.75, juce::Colour(isBinaural ? kRed : kAmber));
        g.setGradientFill(grad);
        g.fillRect(x, y, fillW, kBarH);
    }

    // Hold marker
    if (ch.hold > 0.001f) {
        const float holdX = x + linearToNorm(ch.hold) * w;
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillRect(holdX, y, 1.5f, kBarH);
    }

    // Border
    g.setColour(juce::Colour(kBorder));
    g.drawRect(x, y, w, kBarH, 1.0f);
}

void MultiChannelMeter::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth();

    g.fillAll(juce::Colour(kBg));

    const float barX  = kTagW + 4.0f;
    const float barW  = W - barX - kDbW - 6.0f;

    auto drawSection = [&](const char* title, float& yPos) {
        g.setColour(juce::Colour(kMuted));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
        g.drawText(title, 0, (int)yPos, (int)W, 12,
                   juce::Justification::centredLeft);
        yPos += 14.0f;
    };

    float y = 8.0f;

    // ── LEVELS section ─────────────────────────────────────────────────────
    drawSection("LEVELS", y);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f)));

    for (int i = 0; i < numChannels_; ++i) {
        const bool isLFE = (i == 3);

        // Tag
        g.setColour(isLFE ? juce::Colour(kLFE) : juce::Colour(kMuted));
        g.drawText(channelTag(i), 0, (int)y, (int)kTagW, (int)kBarH,
                   juce::Justification::centredRight);

        // Bar
        drawBar(g, barX, y, barW, inputs_[i]);

        // dB readout
        g.setColour(juce::Colour(kMuted));
        g.drawText(dbText(inputs_[i].display),
                   (int)(barX + barW + 3), (int)y,
                   (int)kDbW, (int)kBarH,
                   juce::Justification::centredLeft);

        y += kBarH + kGap;
    }

    y += kSectionGap;

    // ── BINAURAL OUT section ───────────────────────────────────────────────
    drawSection("BINAURAL OUT", y);

    for (auto& [ch, label] : std::array<std::pair<Chan*, const char*>, 2>{
            std::pair<Chan*, const char*>{&binauralL_, "L"},
            std::pair<Chan*, const char*>{&binauralR_, "R"}}) {
        g.setColour(juce::Colour(kMuted));
        g.drawText(label, 0, (int)y, (int)kTagW, (int)kBarH,
                   juce::Justification::centredRight);
        drawBar(g, barX, y, barW, *ch, true);
        g.setColour(juce::Colour(kMuted));
        g.drawText(dbText(ch->display),
                   (int)(barX + barW + 3), (int)y,
                   (int)kDbW, (int)kBarH,
                   juce::Justification::centredLeft);
        y += kBarH + kGap;
    }
}

} // namespace hearGOD
