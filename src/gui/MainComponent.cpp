#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

static constexpr uint32_t kBg      = 0xFF0D0D0F;
static constexpr uint32_t kSurface = 0xFF141418;
static constexpr uint32_t kBorder  = 0xFF2A2A35;
static constexpr uint32_t kAccent  = 0xFF7C5CBF;
static constexpr uint32_t kText    = 0xFFE4E4E7;
static constexpr uint32_t kMuted   = 0xFF71717A;
static constexpr uint32_t kGreen   = 0xFF22C55E;
static constexpr uint32_t kRed     = 0xFFEF4444;

MainComponent::MainComponent()
    : processor_(AudioConfig{})
{
    propsOptions_.applicationName     = "hearGOD";
    propsOptions_.filenameSuffix      = ".xml";
    propsOptions_.osxLibrarySubFolder = "Application Support";
    props_ = std::make_unique<juce::PropertiesFile>(propsOptions_);

    setSize(1100, 680);
    setupDeviceManager();
    setupControls();
    restoreState();
    startTimerHz(50);
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveState();
    deviceManager_.removeChangeListener(this);
    deviceManager_.removeAudioCallback(&processor_);
}

void MainComponent::timerCallback()
{
    const auto& m = processor_.metrics();

    // Feed per-channel peaks to multi-meter
    for (int i = 0; i < MAX_CHANNELS; ++i)
        multiMeter_.setInputPeak(i, m.inputPeak[i].load(std::memory_order_relaxed));

    multiMeter_.setBinauralPeak(
        m.binauralPeakL.load(std::memory_order_relaxed),
        m.binauralPeakR.load(std::memory_order_relaxed));

    const int xruns = m.xrunCount.load(std::memory_order_relaxed);
    xrunLabel_.setText("Xruns: " + juce::String(xruns), juce::dontSendNotification);
    xrunLabel_.setColour(juce::Label::textColourId,
                         juce::Colour(xruns == 0 ? kGreen : kRed));
    if (xruns > lastXruns_) {
        logPanel_.log("Xrun detected (total: " + juce::String(xruns) + ")",
                      LogPanel::Level::Warn);
        lastXruns_ = xruns;
    }

    if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
        const double sr      = dev->getCurrentSampleRate();
        const int    frames  = processor_.config().bufferFrames;
        const double latMs   = sr > 0.0 ? (frames / sr) * 1000.0 : 0.0;
        latencyLabel_.setText(juce::String(latMs, 1) + " ms  |  "
                              + juce::String(frames) + " frames  |  "
                              + juce::String(static_cast<int>(sr)) + " Hz",
                              juce::dontSendNotification);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop(kHeaderH);
    swapLRBtn_.setBounds(header.removeFromRight(90).reduced(8, 12));
    modeBtn_.setBounds(header.removeFromRight(110).reduced(8, 12));
    // Device combos — sit right of logo (logo+version occupies ~200px)
    header.removeFromLeft(200); // skip logo area
    auto combosArea = header.reduced(0, 10);
    inputDeviceCombo_.setBounds(combosArea.removeFromLeft(190));
    combosArea.removeFromLeft(8);
    outputDeviceCombo_.setBounds(combosArea.removeFromLeft(190));

    // Log panel (above status bar when visible)
    if (logVisible_)
        logPanel_.setBounds(area.removeFromBottom(180));

    // Status bar
    auto statusArea = area.removeFromBottom(kStatusH);
    logToggleBtn_.setBounds(statusArea.removeFromRight(56).reduced(4, 4));
    statusBar_.setBounds(statusArea.reduced(8, 4));

    // Three columns
    auto leftCol  = area.removeFromLeft(kLeftW);
    auto rightCol = area.removeFromRight(kRightW);
    auto centre   = area;

    hrirPanel_.setBounds(leftCol);

    // Right: meters on top, xrun label at bottom
    auto right = rightCol.reduced(6);
    xrunLabel_.setBounds(right.removeFromBottom(16));
    latencyLabel_.setBounds(right.removeFromBottom(16));
    multiMeter_.setBounds(right);

    // Centre: EQ controls + graph
    auto c = centre.reduced(8);

    // Row 1: profile combo + add/remove/save + Raw FR + Target + Load EQ
    auto row1 = c.removeFromTop(26);
    eqBrowseBtn_.setBounds(row1.removeFromRight(84));
    row1.removeFromRight(4);
    targetCurveBtn_.setBounds(row1.removeFromRight(68));
    row1.removeFromRight(4);
    rawFRBtn_.setBounds(row1.removeFromRight(72));
    row1.removeFromRight(4);
    eqSaveProfileBtn_.setBounds(row1.removeFromRight(44));
    row1.removeFromRight(2);
    eqRemoveProfileBtn_.setBounds(row1.removeFromRight(24));
    row1.removeFromRight(2);
    eqAddProfileBtn_.setBounds(row1.removeFromRight(24));
    row1.removeFromRight(4);
    eqProfileCombo_.setBounds(row1);

    // Row 2: EQ status | Norm Hz | offset
    c.removeFromTop(4);
    auto row2 = c.removeFromTop(22);
    rawFROffsetLabel_.setBounds(row2.removeFromRight(56));
    rawFROffsetSlider_.setBounds(row2.removeFromRight(100));
    row2.removeFromRight(8);
    normFreqCombo_.setBounds(row2.removeFromRight(76));
    row2.removeFromRight(4);
    eqStatusLabel_.setBounds(row2);
    c.removeFromTop(4);

    eqGraph_.setBounds(c);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBg));

    // Header
    auto b = getLocalBounds();
    g.setColour(juce::Colour(kSurface));
    g.fillRect(b.removeFromTop(kHeaderH));
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(kHeaderH, 0.0f, (float)getWidth());

    // Logo
    g.setColour(juce::Colour(kAccent));
    g.fillRoundedRectangle(12.0f, 12.0f, 28.0f, 28.0f, 5.0f);
    g.setColour(juce::Colour(kText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(15.0f).withStyle("Bold")));
    g.drawText("hearGOD", 50, 12, 120, 28, juce::Justification::centredLeft);
    g.setColour(juce::Colour(kMuted));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("v0.1.0", 152, 16, 40, 16, juce::Justification::centredLeft);

    // Column dividers
    const int leftEdge  = kLeftW;
    const int rightEdge = getWidth() - kRightW;
    g.setColour(juce::Colour(kBorder));
    g.drawVerticalLine(leftEdge,  (float)kHeaderH, (float)(getHeight() - kStatusH));
    g.drawVerticalLine(rightEdge, (float)kHeaderH, (float)(getHeight() - kStatusH));

    // Status bar bg
    g.setColour(juce::Colour(kSurface));
    g.fillRect(0, getHeight() - kStatusH, getWidth(), kStatusH);
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(getHeight() - kStatusH, 0.0f, (float)getWidth());

    // Centre label
    g.setColour(juce::Colour(kText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
    g.drawText("TARGET EQ  -  FREQUENCY RESPONSE",
               leftEdge + 8, kHeaderH + 4, 300, 14,
               juce::Justification::centredLeft);
}

} // namespace hearGOD
