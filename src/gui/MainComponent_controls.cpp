#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

static constexpr uint32_t kSurface  = 0xFF141418;
static constexpr uint32_t kElevated = 0xFF1C1C23;
static constexpr uint32_t kAccent   = 0xFF7C5CBF;
static constexpr uint32_t kText     = 0xFFE4E4E7;
static constexpr uint32_t kMuted    = 0xFF71717A;
static constexpr uint32_t kGreen    = 0xFF22C55E;

void MainComponent::setupControls()
{
    // Left — HRIR panel
    hrirPanel_.onSofaLoaded = [this](const juce::File& f) { loadSOFA(f); };
    hrirPanel_.onMasterGainChanged = [this](float db) {
        processor_.setMasterGainDb(db);
        logPanel_.log("Master gain -> " + juce::String(db, 1) + " dB");
    };
    addAndMakeVisible(hrirPanel_);

    // Centre — EQ
    addAndMakeVisible(eqGraph_);
    processor_.setWaveformSink(&eqGraph_,
        [](void* obj, const float* mono, int n) {
            static_cast<EQGraph*>(obj)->pushWaveformSamples(mono, n);
        });

    // EQ profile combo
    eqProfileCombo_.setTextWhenNothingSelected("No EQ preset");
    eqProfileCombo_.setEditableText(true);
    eqProfileCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1C1C23));
    eqProfileCombo_.setColour(juce::ComboBox::textColourId,       juce::Colour(0xFFE4E4E7));
    eqProfileCombo_.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xFF2A2A35));
    eqProfileCombo_.setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xFF71717A));
    eqProfileCombo_.onChange = [this] {
        const int idx = eqProfileCombo_.getSelectedItemIndex();
        if (idx >= 0) {
            // Rename if text was edited
            const juce::String newName = eqProfileCombo_.getText().trim();
            if (newName.isNotEmpty() && newName != eqProfiles_[idx].name) {
                eqProfiles_.getReference(idx).name = newName;
                eqProfileCombo_.changeItemText(idx + 1, newName);
                saveProfiles();
                logPanel_.log("Profile renamed: " + newName);
            }
            selectEQProfile(idx);
        }
    };
    addAndMakeVisible(eqProfileCombo_);

    // "+" add profile button
    eqAddProfileBtn_.setButtonText("+");
    eqAddProfileBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kElevated));
    eqAddProfileBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    eqAddProfileBtn_.onClick = [this] {
        eqFileChooser_ = std::make_unique<juce::FileChooser>(
            "Add EQ profile",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.txt");
        eqFileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile()) addEQProfile(f);
            });
    };
    addAndMakeVisible(eqAddProfileBtn_);

    // "-" remove profile button
    eqRemoveProfileBtn_.setButtonText("-");
    eqRemoveProfileBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kElevated));
    eqRemoveProfileBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    eqRemoveProfileBtn_.onClick = [this] { removeActiveEQProfile(); };
    addAndMakeVisible(eqRemoveProfileBtn_);

    // Save current state into active profile
    eqSaveProfileBtn_.setButtonText("Save");
    eqSaveProfileBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kElevated));
    eqSaveProfileBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kAccent));
    eqSaveProfileBtn_.onClick = [this] { saveActiveProfile(); };
    addAndMakeVisible(eqSaveProfileBtn_);

    eqBrowseBtn_.setButtonText("Load EQ...");
    eqBrowseBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kElevated));
    eqBrowseBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    eqBrowseBtn_.onClick = [this] {
        eqFileChooser_ = std::make_unique<juce::FileChooser>(
            "Open EQ preset",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.txt");
        eqFileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile()) loadEQ(f);
            });
    };
    addAndMakeVisible(eqBrowseBtn_);

    // Raw FR button (cyan — headphone measurement)
    rawFRBtn_.setButtonText("Raw FR...");
    rawFRBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kElevated));
    rawFRBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF22D3EE));
    rawFRBtn_.onClick = [this] {
        rawFRFileChooser_ = std::make_unique<juce::FileChooser>(
            "Load headphone frequency response",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.csv;*.txt");
        rawFRFileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile()) loadRawFR(f);
            });
    };
    addAndMakeVisible(rawFRBtn_);

    // Target curve button (white — Harman / PEQdB Diamond etc.)
    targetCurveBtn_.setButtonText("Target...");
    targetCurveBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(kElevated));
    targetCurveBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFF1F5F9));
    targetCurveBtn_.onClick = [this] {
        targetFileChooser_ = std::make_unique<juce::FileChooser>(
            "Load target curve",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.csv;*.txt");
        targetFileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile()) loadTargetCurve(f);
            });
    };
    addAndMakeVisible(targetCurveBtn_);

    // Norm Hz combo
    normFreqCombo_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1C1C23));
    normFreqCombo_.setColour(juce::ComboBox::textColourId,       juce::Colour(0xFFE4E4E7));
    normFreqCombo_.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xFF2A2A35));
    normFreqCombo_.setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xFF71717A));
    normFreqCombo_.addItem("20 Hz",   1);
    normFreqCombo_.addItem("100 Hz",  2);
    normFreqCombo_.addItem("500 Hz",  3);
    normFreqCombo_.addItem("1 kHz",   4);
    normFreqCombo_.addItem("2 kHz",   5);
    normFreqCombo_.addItem("5 kHz",   6);
    normFreqCombo_.setSelectedId(4, juce::dontSendNotification); // 1 kHz default
    normFreqCombo_.onChange = [this] {
        constexpr float kFreqs[] = { 20.0f, 100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };
        const int idx = normFreqCombo_.getSelectedId() - 1;
        if (idx >= 0 && idx < 6)
            eqGraph_.setNormFreq(kFreqs[idx]);
    };
    addAndMakeVisible(normFreqCombo_);

    // Raw FR offset slider
    rawFROffsetSlider_.setSliderStyle(juce::Slider::IncDecButtons);
    rawFROffsetSlider_.setRange(-20.0, 20.0, 0.5);
    rawFROffsetSlider_.setValue(0.0);
    rawFROffsetSlider_.setColour(juce::Slider::textBoxTextColourId,       juce::Colour(kText));
    rawFROffsetSlider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF1C1C23));
    rawFROffsetSlider_.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xFF2A2A35));
    rawFROffsetSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 46, 22);
    rawFROffsetSlider_.onValueChange = [this] {
        eqGraph_.setRawFROffset((float)rawFROffsetSlider_.getValue());
    };
    addAndMakeVisible(rawFROffsetSlider_);

    rawFROffsetLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    rawFROffsetLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    rawFROffsetLabel_.setText("Offset dB", juce::dontSendNotification);
    addAndMakeVisible(rawFROffsetLabel_);

    eqStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    eqStatusLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    eqStatusLabel_.setText("No EQ preset", juce::dontSendNotification);
    addAndMakeVisible(eqStatusLabel_);

    // Right — multi-channel meter
    addAndMakeVisible(multiMeter_);

    // Xrun label
    xrunLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreen));
    xrunLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    xrunLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(xrunLabel_);

    latencyLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));
    latencyLabel_.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    latencyLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(latencyLabel_);

    // Device selectors
    auto styleCombo = [](juce::ComboBox& c) {
        c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1C1C23));
        c.setColour(juce::ComboBox::textColourId,       juce::Colour(0xFFE4E4E7));
        c.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xFF2A2A35));
        c.setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xFF71717A));
        c.setTextWhenNothingSelected("IN device...");
    };
    styleCombo(inputDeviceCombo_);
    styleCombo(outputDeviceCombo_);
    outputDeviceCombo_.setTextWhenNothingSelected("OUT device...");
    inputDeviceCombo_.onChange  = [this] { applyDeviceSelection(); };
    outputDeviceCombo_.onChange = [this] { applyDeviceSelection(); };
    addAndMakeVisible(inputDeviceCombo_);
    addAndMakeVisible(outputDeviceCombo_);
    populateDeviceCombos();

    // Mode toggle
    modeBtn_.setButtonText("Binaural");
    modeBtn_.setClickingTogglesState(true);
    modeBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(kElevated));
    modeBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF5A3FA0));
    modeBtn_.setColour(juce::TextButton::textColourOffId,  juce::Colour(kText));
    modeBtn_.onClick = [this] {
        stereoEqMode_ = modeBtn_.getToggleState();
        modeBtn_.setButtonText(stereoEqMode_ ? "Stereo EQ" : "Binaural");
        processor_.setStereoEqMode(stereoEqMode_);
        multiMeter_.setActiveChannels(stereoEqMode_ ? 2 : processor_.config().inputChannels);
        hrirPanel_.setStereoMode(stereoEqMode_);

        {
            const juce::String targetDevice = stereoEqMode_ ? "BlackHole 2ch" : "BlackHole 16ch";

            // Stop audio before touching HAL — prevents race with HALC_ShellObject_Listener
            deviceManager_.removeAudioCallback(&processor_);

            const bool ok = setSystemOutputDevice(targetDevice);
            logPanel_.log(ok ? ("System output -> " + targetDevice)
                             : ("WARNING: " + targetDevice + " not found"),
                          ok ? LogPanel::Level::Info : LogPanel::Level::Warn);

            // Update input combo selection (no notification — apply deferred below)
            for (int i = 1; i <= inputDeviceCombo_.getNumItems(); ++i) {
                if (inputDeviceCombo_.getItemText(i - 1).startsWith(targetDevice)) {
                    inputDeviceCombo_.setSelectedId(i, juce::dontSendNotification);
                    break;
                }
            }

            // Defer device apply by 200ms — let HAL settle after system output switch
            juce::Timer::callAfterDelay(200, [this] {
                applyDeviceSelection();
                deviceManager_.addAudioCallback(&processor_);
            });
        }

        logPanel_.log(juce::String("Mode -> ") + (stereoEqMode_ ? "Stereo EQ" : "Binaural"));
    };
    addAndMakeVisible(modeBtn_);

    swapLRBtn_.setButtonText("L/R Swap");
    swapLRBtn_.setClickingTogglesState(true);
    swapLRBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(kElevated));
    swapLRBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7C3AED));
    swapLRBtn_.setColour(juce::TextButton::textColourOffId,  juce::Colour(kText));
    swapLRBtn_.onClick = [this] {
        processor_.setSwapLR(swapLRBtn_.getToggleState());
    };
    addAndMakeVisible(swapLRBtn_);

    // Status bar
    statusBar_.setColour(juce::Label::textColourId,       juce::Colour(kMuted));
    statusBar_.setColour(juce::Label::backgroundColourId, juce::Colour(kSurface));
    statusBar_.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    statusBar_.setText("hearGOD v0.1.0", juce::dontSendNotification);
    statusBar_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusBar_);

    // Log panel + toggle
    addAndMakeVisible(logPanel_);
    logPanel_.setVisible(false);

    logToggleBtn_.setButtonText("Logs");
    logToggleBtn_.setClickingTogglesState(true);
    logToggleBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF1C1C23));
    logToggleBtn_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF2A2A35));
    logToggleBtn_.setColour(juce::TextButton::textColourOffId,  juce::Colour(kMuted));
    logToggleBtn_.setColour(juce::TextButton::textColourOnId,   juce::Colour(kText));
    logToggleBtn_.onClick = [this] {
        logVisible_ = logToggleBtn_.getToggleState();
        logPanel_.setVisible(logVisible_);
        resized();
    };
    addAndMakeVisible(logToggleBtn_);

    logPanel_.log("hearGOD v0.1.0 started");
}

} // namespace hearGOD
