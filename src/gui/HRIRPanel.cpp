#include "HRIRPanel.h"
#include <cmath>

namespace hearGOD {

static constexpr uint32_t kBg      = 0xFF141418;
static constexpr uint32_t kSurface = 0xFF1C1C23;
static constexpr uint32_t kBorder  = 0xFF2A2A35;
static constexpr uint32_t kAccent  = 0xFF7C5CBF;
static constexpr uint32_t kText    = 0xFFE4E4E7;
static constexpr uint32_t kMuted   = 0xFF71717A;
static constexpr uint32_t kGreen   = 0xFF22C55E;
static constexpr uint32_t kRed     = 0xFFEF4444;
static constexpr uint32_t kLFE     = 0xFF8B5CF6;

static const std::vector<ChannelMappingRow> kDefault16Rows = {
    {"L",   "Front Left",         -30.0f,  0.0f},
    {"R",   "Front Right",        +30.0f,  0.0f},
    {"C",   "Center",              0.0f,   0.0f},
    {"LFE", "Low Freq Effects",    0.0f,   0.0f, true},
    {"Ls",  "Side Left",          -90.0f,  0.0f},
    {"Rs",  "Side Right",         +90.0f,  0.0f},
    {"Lss", "Surround Left",     -135.0f,  0.0f},
    {"Rss", "Surround Right",    +135.0f,  0.0f},
    {"TFL", "Top Front Left",     -45.0f, 45.0f},
    {"TFR", "Top Front Right",    +45.0f, 45.0f},
    {"TBL", "Top Back Left",     -135.0f, 45.0f},
    {"TBR", "Top Back Right",    +135.0f, 45.0f},
    {"TSL", "Top Side Left",      -90.0f, 45.0f},
    {"TSR", "Top Side Right",     +90.0f, 45.0f},
    {"BL",  "Back Left",         -135.0f,  0.0f},
    {"BR",  "Back Right",        +135.0f,  0.0f},
};

HRIRPanel::HRIRPanel()
    : rows_(kDefault16Rows)
{
    browseBtn_.setButtonText("Browse...");
    browseBtn_.onClick = [this] { openFilePicker(); };
    browseBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kAccent));
    browseBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    addAndMakeVisible(browseBtn_);

    styleLabel(sofaNameLabel_,   juce::Colour(kText),  11.0f, true);
    styleLabel(sofaMetaLabel_,   juce::Colour(kMuted), 10.0f);
    styleLabel(sofaStatusLabel_, juce::Colour(kMuted), 10.0f);
    sofaNameLabel_.setText("No SOFA file loaded", juce::dontSendNotification);
    addAndMakeVisible(sofaNameLabel_);
    addAndMakeVisible(sofaMetaLabel_);
    addAndMakeVisible(sofaStatusLabel_);

    // Master gain rotary
    masterGainSlider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    masterGainSlider_.setRange(-40.0, 12.0, 0.1);
    masterGainSlider_.setValue(0.0);
    masterGainSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterGainSlider_.setColour(juce::Slider::thumbColourId,             juce::Colour(kAccent));
    masterGainSlider_.setColour(juce::Slider::rotarySliderFillColourId,  juce::Colour(kAccent));
    masterGainSlider_.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(kBorder));
    masterGainSlider_.onValueChange = [this] {
        const float db = (float)masterGainSlider_.getValue();
        masterGainValueLabel_.setText(juce::String(db, 1) + "\ndB",
                                      juce::dontSendNotification);
        if (onMasterGainChanged) onMasterGainChanged(db);
    };
    addAndMakeVisible(masterGainSlider_);

    styleLabel(masterGainValueLabel_, juce::Colour(kText), 13.0f, true);
    masterGainValueLabel_.setText("0.0\ndB", juce::dontSendNotification);
    masterGainValueLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterGainValueLabel_);

    styleLabel(masterGainTitleLabel_, juce::Colour(kMuted), 9.5f);
    masterGainTitleLabel_.setText("MASTER GAIN", juce::dontSendNotification);
    masterGainTitleLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterGainTitleLabel_);
}

void HRIRPanel::styleLabel(juce::Label& l, juce::Colour c, float sz, bool bold)
{
    l.setColour(juce::Label::textColourId, c);
    l.setFont(juce::Font(juce::FontOptions{}.withHeight(sz)
                                            .withStyle(bold ? "Bold" : "Regular")));
    l.setJustificationType(juce::Justification::centredLeft);
}

void HRIRPanel::setChannelMapping(const std::vector<ChannelMappingRow>& rows)
{
    rows_ = rows;
    repaint();
}

void HRIRPanel::setNumChannels(int n)
{
    numChannels_ = n;
    repaint();
}

void HRIRPanel::setStereoMode(bool isStereo)
{
    stereoMode_ = isStereo;
    repaint();
}

void HRIRPanel::setMasterGain(float db)
{
    masterGainSlider_.setValue(db, juce::dontSendNotification);
    masterGainValueLabel_.setText(juce::String(db, 1) + "\ndB",
                                  juce::dontSendNotification);
}

void HRIRPanel::showSOFAInfo(const juce::String& path, int measurements,
                              int irLength, int sampleRate)
{
    const juce::File f(path);
    sofaNameLabel_.setText(f.getFileName(), juce::dontSendNotification);
    sofaMetaLabel_.setText(
        juce::String(measurements) + " pos  ·  " +
        juce::String(irLength) + "-tap  ·  " +
        juce::String(sampleRate / 1000) + "kHz",
        juce::dontSendNotification);
    sofaStatusLabel_.setText("Loaded", juce::dontSendNotification);
    sofaStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreen));
}

void HRIRPanel::showError(const juce::String& msg)
{
    sofaStatusLabel_.setText(msg, juce::dontSendNotification);
    sofaStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kRed));
}

void HRIRPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(16); // space for "HRIR / SOFA" section label

    // SOFA section
    browseBtn_.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    sofaNameLabel_.setBounds(area.removeFromTop(16));
    sofaMetaLabel_.setBounds(area.removeFromTop(14));
    sofaStatusLabel_.setBounds(area.removeFromTop(14));
    area.removeFromTop(6);

    // Channel list fills remaining space above gain section
    auto gainArea = area.removeFromBottom((int)kGainH);
    // (channel rows drawn in paint)
    (void)area;

    // Master gain at bottom
    masterGainTitleLabel_.setBounds(gainArea.removeFromTop(14));
    gainArea.removeFromTop(2);
    auto gainRow = gainArea;
    masterGainSlider_.setBounds(gainRow.removeFromLeft(60).reduced(0, 4));
    masterGainValueLabel_.setBounds(gainRow.reduced(0, 8));
}

void HRIRPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBg));

    const auto b = getLocalBounds();
    const float W = (float)b.getWidth();

    // Section header "HRIR / SOFA"
    g.setColour(juce::Colour(kMuted));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
    g.drawText("HRIR / SOFA", 8, 3, (int)W - 16, 14, juce::Justification::centredLeft);

    // Divider after SOFA section
    const int sofaDivY = 14 + 26 + 6 + 16 + 14 + 14 + 6;
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(sofaDivY, 8.0f, W - 8.0f);

    // Channel mapping section header
    const int mapY = sofaDivY + 4;
    g.setColour(juce::Colour(kMuted));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.5f).withStyle("Bold")));
    g.drawText("CHANNEL MAPPING  -  7.1", 8, mapY, (int)W - 16, 12,
               juce::Justification::centredLeft);

    // Channel rows
    const int gainTop = b.getHeight() - (int)kGainH;
    float rowY = (float)(mapY + 14);
    const float tagW  = 30.0f;
    const float nameW = W - tagW - 80.0f - 16.0f;
    const float azW   = 80.0f;

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));

    for (int i = 0; i < numChannels_ && i < (int)rows_.size(); ++i) {
        if (rowY + kRowH > (float)gainTop - 4) break;
        const auto& row = rows_[i];

        // In stereo mode, dim all channels except L (0) and R (1)
        const bool dimmed = stereoMode_ && i >= 2;
        const float alpha = dimmed ? 0.25f : 1.0f;

        // Tag chip
        auto tagCol = row.isLFE ? juce::Colour(kLFE) : juce::Colour(kAccent);
        g.setColour(tagCol.withAlpha(alpha));
        g.fillRoundedRectangle(8.0f, rowY + 3.0f, tagW - 4.0f, kRowH - 6.0f, 2.0f);
        g.setColour(juce::Colour(kText).withAlpha(alpha));
        g.drawText(row.tag, 8, (int)rowY, (int)tagW - 4, (int)kRowH,
                   juce::Justification::centred);

        // Channel name
        if (row.isLFE) {
            g.setColour(juce::Colour(kMuted).withAlpha(alpha));
            g.drawText("LPF 120Hz", (int)(8 + tagW), (int)rowY,
                       (int)nameW, (int)kRowH, juce::Justification::centredLeft);
        } else {
            g.setColour(juce::Colour(kText).withAlpha(alpha));
            g.drawText(row.name, (int)(8 + tagW), (int)rowY,
                       (int)nameW, (int)kRowH, juce::Justification::centredLeft);
        }

        // Az / El
        if (!row.isLFE) {
            g.setColour(juce::Colour(kMuted).withAlpha(alpha));
            const juce::String azEl =
                juce::String((int)row.azimuth) + "° / " +
                juce::String((int)row.elevation) + "°";
            g.drawText(azEl, (int)(W - azW - 8), (int)rowY,
                       (int)azW, (int)kRowH, juce::Justification::centredRight);
        }

        rowY += kRowH;
    }

    // Divider above gain
    g.setColour(juce::Colour(kBorder));
    g.drawHorizontalLine(gainTop - 2, 8.0f, W - 8.0f);
}

void HRIRPanel::openFilePicker()
{
    fileChooser_ = std::make_unique<juce::FileChooser>(
        "Open SOFA HRIR file",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "*.sofa");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f.existsAsFile() && onSofaLoaded)
                onSofaLoaded(f);
        });
}

} // namespace hearGOD
