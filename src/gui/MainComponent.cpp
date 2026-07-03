#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

static constexpr uint32_t kBg       = 0xFF0D0D0F;
static constexpr uint32_t kSurface  = 0xFF141418;
static constexpr uint32_t kElevated = 0xFF1C1C23;
static constexpr uint32_t kBorder   = 0xFF2A2A35;
static constexpr uint32_t kAccent   = 0xFF7C5CBF;
static constexpr uint32_t kText     = 0xFFE4E4E7;
static constexpr uint32_t kMuted    = 0xFF71717A;
static constexpr uint32_t kGreen    = 0xFF22C55E;
static constexpr uint32_t kRed      = 0xFFEF4444;

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

void MainComponent::setupDeviceManager()
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    // No SR hint — let JUCE negotiate native device SR.
    setup.bufferSize    = BUFFER_FRAMES;
    setup.inputChannels = (juce::BigInteger(1) << 16) - 1; // bits 0-15
    setup.outputChannels = 3;

    const juce::String err = deviceManager_.initialise(16, 2, nullptr, true, {}, &setup);
    if (err.isNotEmpty())
        juce::Logger::writeToLog("AudioDeviceManager init: " + err);

    deviceManager_.addAudioCallback(&processor_);
    deviceManager_.addChangeListener(this);
}

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

void MainComponent::populateDeviceCombos()
{
    const auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (!type) return;

    inputDeviceCombo_.clear(juce::dontSendNotification);
    outputDeviceCombo_.clear(juce::dontSendNotification);

    const auto inNames  = type->getDeviceNames(true);
    const auto outNames = type->getDeviceNames(false);

    for (int i = 0; i < inNames.size(); ++i)
        inputDeviceCombo_.addItem(inNames[i], i + 1);
    for (int i = 0; i < outNames.size(); ++i)
        outputDeviceCombo_.addItem(outNames[i], i + 1);

    // Pre-select current device
    if (auto* dev = deviceManager_.getCurrentAudioDevice()) {
        const auto currentIn  = dev->getName();
        const auto currentOut = dev->getName();
        for (int i = 0; i < inNames.size(); ++i)
            if (inNames[i] == currentIn) { inputDeviceCombo_.setSelectedId(i + 1, juce::dontSendNotification); break; }
        for (int i = 0; i < outNames.size(); ++i)
            if (outNames[i] == currentOut) { outputDeviceCombo_.setSelectedId(i + 1, juce::dontSendNotification); break; }
    }
}

void MainComponent::applyDeviceSelection()
{
    const auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (!type) return;

    const int inIdx  = inputDeviceCombo_.getSelectedId()  - 1;
    const int outIdx = outputDeviceCombo_.getSelectedId() - 1;

    const auto inNames  = type->getDeviceNames(true);
    const auto outNames = type->getDeviceNames(false);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    if (inIdx  >= 0 && inIdx  < inNames.size())  setup.inputDeviceName  = inNames[inIdx];
    if (outIdx >= 0 && outIdx < outNames.size()) setup.outputDeviceName = outNames[outIdx];

    setup.useDefaultInputChannels  = true;
    setup.useDefaultOutputChannels = true;

    const juce::String err = deviceManager_.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty()) {
        statusBar_.setText("Device error: " + err, juce::dontSendNotification);
        logPanel_.log("Device error: " + err, LogPanel::Level::Error);
    } else {
        statusBar_.setText("IN: " + setup.inputDeviceName + "   OUT: " + setup.outputDeviceName,
                           juce::dontSendNotification);
        logPanel_.log("Device -> IN: " + setup.inputDeviceName + "  OUT: " + setup.outputDeviceName);
    }
}

void MainComponent::loadSOFA(const juce::File& f)
{
    const juce::String path = f.getFullPathName();

    SOFALoader sofa;
    const int sr = static_cast<int>(deviceManager_.getCurrentAudioDevice()
                                    ? deviceManager_.getCurrentAudioDevice()->getCurrentSampleRate()
                                    : 48000.0);
    if (!sofa.load(path.toStdString(), sr)) {
        hrirPanel_.showError("Failed to load SOFA");
        logPanel_.log("SOFA load failed: " + f.getFileName(), LogPanel::Level::Error);
        return;
    }

    const AudioConfig& cfg = processor_.config();
    auto engine = std::make_shared<NUOLSEngine>(cfg.bufferFrames, 4);
    engine->setMasterGainDb(cfg.masterGainDb);
    engine->setLfeGainDb(cfg.lfeGainDb);

    int loaded = 0;
    for (int i = 0; i < cfg.inputChannels && i < MAX_CHANNELS; ++i) {
        auto ch = static_cast<Channel>(i);
        if (ch == Channel::LFE || ch == Channel::INVALID) continue;
        auto hrir = sofa.getHRIRForChannel(ch);
        if (!hrir) continue;
        engine->loadHRIR(ch, hrir->left.data(), hrir->right.data(), hrir->irLength);
        ++loaded;
    }

    {
        auto allHrirs = sofa.getAllHRIRs();
        DFECompensator dfe;
        dfe.compute(allHrirs, sofa.irLength());
        if (dfe.isReady()) {
            for (int i = 0; i < cfg.inputChannels && i < MAX_CHANNELS; ++i) {
                auto ch = static_cast<Channel>(i);
                if (ch == Channel::LFE || ch == Channel::INVALID) continue;
                auto hrir = sofa.getHRIRForChannel(ch);
                if (!hrir) continue;
                auto compL = dfe.apply(hrir->left.data(),  hrir->irLength);
                auto compR = dfe.apply(hrir->right.data(), hrir->irLength);
                engine->loadHRIR(ch, compL.data(), compR.data(), hrir->irLength);
            }
        }
    }

    processor_.setEngine(std::move(engine));

    const int measurements = (int)(sofa.getAllHRIRs().size() / 2);
    hrirPanel_.showSOFAInfo(path, measurements, sofa.irLength(), sr);
    multiMeter_.setActiveChannels(cfg.inputChannels);
    statusBar_.setText("SOFA loaded: " + f.getFileName() +
                       "  (" + juce::String(loaded) + " channels)",
                       juce::dontSendNotification);
    lastSofaPath_ = f.getFullPathName();
    logPanel_.log("SOFA loaded: " + f.getFileName());
    logPanel_.log("  IR length: " + juce::String(sofa.irLength()) +
                  " taps  |  " + juce::String(loaded) + " channels  |  " +
                  juce::String(measurements) + " positions");
}

void MainComponent::loadEQ(const juce::File& f)
{
    const float sr = static_cast<float>(
        deviceManager_.getCurrentAudioDevice()
            ? deviceManager_.getCurrentAudioDevice()->getCurrentSampleRate()
            : 48000.0);
    auto preset = parsePEQ(f.getFullPathName().toStdString(), sr);
    if (!preset) {
        eqStatusLabel_.setText("EQ parse error", juce::dontSendNotification);
        eqStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kRed));
        logPanel_.log("EQ parse failed: " + f.getFileName(), LogPanel::Level::Error);
        return;
    }

    auto eq = std::make_shared<BiquadChain>();
    eq->setFilters(buildCoeffs(*preset, static_cast<float>(sr)));
    eq->setPreamp(preset->preampDb);
    processor_.setEQ(std::move(eq));

    eqGraph_.setPreset(*preset, static_cast<float>(sr));
    eqStatusLabel_.setText(
        juce::String((int)preset->filters.size()) + " filters, preamp " +
        juce::String(preset->preampDb, 1) + " dB",
        juce::dontSendNotification);
    eqStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreen));
    lastEqPath_ = f.getFullPathName();
    statusBar_.setText("EQ loaded: " + f.getFileName(), juce::dontSendNotification);
    logPanel_.log("EQ loaded: " + f.getFileName());
    logPanel_.log("  " + juce::String((int)preset->filters.size()) +
                  " filters  |  preamp " + juce::String(preset->preampDb, 1) + " dB");
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
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop(kHeaderH);
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

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // Device list changed (plug/unplug) — repopulate combos, preserve selection
    const juce::String prevIn  = inputDeviceCombo_.getText();
    const juce::String prevOut = outputDeviceCombo_.getText();
    populateDeviceCombos();
    // Restore previous selection if device still present
    for (int i = 1; i <= inputDeviceCombo_.getNumItems(); ++i)
        if (inputDeviceCombo_.getItemText(i - 1) == prevIn)
            { inputDeviceCombo_.setSelectedId(i, juce::dontSendNotification); break; }
    for (int i = 1; i <= outputDeviceCombo_.getNumItems(); ++i)
        if (outputDeviceCombo_.getItemText(i - 1) == prevOut)
            { outputDeviceCombo_.setSelectedId(i, juce::dontSendNotification); break; }
    logPanel_.log("Audio device list updated");
}

void MainComponent::saveState()
{
    if (!props_) return;
    props_->setValue("sofaPath",    lastSofaPath_);
    props_->setValue("eqPath",      lastEqPath_);
    props_->setValue("rawFRPath",   lastRawFRPath_);
    props_->setValue("targetPath",  lastTargetPath_);
    props_->setValue("normFreqIdx", normFreqCombo_.getSelectedId());
    props_->setValue("rawFROffset", (double)rawFROffsetSlider_.getValue());
    props_->setValue("inDevice",    inputDeviceCombo_.getText());
    props_->setValue("outDevice",   outputDeviceCombo_.getText());
    props_->setValue("masterGain",  (double)processor_.config().masterGainDb);
    props_->setValue("stereoEq",    stereoEqMode_);
    props_->saveIfNeeded();
}

void MainComponent::restoreState()
{
    if (!props_) return;

    // Restore device selection
    const auto inDev  = props_->getValue("inDevice");
    const auto outDev = props_->getValue("outDevice");
    if (inDev.isNotEmpty()) {
        for (int i = 1; i <= inputDeviceCombo_.getNumItems(); ++i)
            if (inputDeviceCombo_.getItemText(i - 1) == inDev)
                { inputDeviceCombo_.setSelectedId(i, juce::dontSendNotification); break; }
    }
    if (outDev.isNotEmpty()) {
        for (int i = 1; i <= outputDeviceCombo_.getNumItems(); ++i)
            if (outputDeviceCombo_.getItemText(i - 1) == outDev)
                { outputDeviceCombo_.setSelectedId(i, juce::dontSendNotification); break; }
    }
    if (inDev.isNotEmpty() || outDev.isNotEmpty())
        applyDeviceSelection();

    // Restore master gain
    const float gain = (float)props_->getDoubleValue("masterGain", -9.5);
    hrirPanel_.setMasterGain(gain);
    processor_.setMasterGainDb(gain);

    // Restore mode
    const bool stereo = props_->getBoolValue("stereoEq", false);
    if (stereo) {
        stereoEqMode_ = true;
        modeBtn_.setToggleState(true, juce::dontSendNotification);
        modeBtn_.setButtonText("Stereo EQ");
        processor_.setStereoEqMode(true);
        multiMeter_.setActiveChannels(2);
        hrirPanel_.setStereoMode(true);
    }

    // Reload SOFA
    const auto sofaPath = props_->getValue("sofaPath");
    if (sofaPath.isNotEmpty()) {
        const juce::File f(sofaPath);
        if (f.existsAsFile()) {
            lastSofaPath_ = sofaPath;
            loadSOFA(f);
            logPanel_.log("Restored SOFA: " + f.getFileName());
        }
    }

    // Restore norm freq + raw FR offset
    const int normIdx = props_->getIntValue("normFreqIdx", 4); // default 1 kHz
    if (normIdx >= 1 && normIdx <= 6) {
        normFreqCombo_.setSelectedId(normIdx, juce::dontSendNotification);
        constexpr float kFreqs[] = { 20.0f, 100.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };
        eqGraph_.setNormFreq(kFreqs[normIdx - 1]);
    }
    const float rawOffset = (float)props_->getDoubleValue("rawFROffset", 0.0);
    rawFROffsetSlider_.setValue(rawOffset, juce::dontSendNotification);
    eqGraph_.setRawFROffset(rawOffset);

    // Reload Raw FR
    const auto rawFRPath = props_->getValue("rawFRPath");
    if (rawFRPath.isNotEmpty()) {
        const juce::File f(rawFRPath);
        if (f.existsAsFile()) {
            lastRawFRPath_ = rawFRPath;
            loadRawFR(f);
        }
    }

    // Reload Target curve
    const auto targetPath = props_->getValue("targetPath");
    if (targetPath.isNotEmpty()) {
        const juce::File f(targetPath);
        if (f.existsAsFile()) {
            lastTargetPath_ = targetPath;
            loadTargetCurve(f);
        }
    }

    // Reload EQ profiles
    restoreProfiles();
}

void MainComponent::addEQProfile(const juce::File& f)
{
    EQProfile p;
    p.name        = f.getFileNameWithoutExtension();
    p.path        = f.getFullPathName();
    p.rawFRPath   = lastRawFRPath_;
    p.targetPath  = lastTargetPath_;
    p.rawFROffset = (float)rawFROffsetSlider_.getValue();
    p.normFreqIdx = normFreqCombo_.getSelectedId();

    // Avoid duplicates by path
    for (int i = 0; i < eqProfiles_.size(); ++i)
        if (eqProfiles_[i].path == p.path) { selectEQProfile(i); return; }

    eqProfiles_.add(p);
    const int newIdx = eqProfiles_.size() - 1;

    eqProfileCombo_.addItem(p.name, newIdx + 1);
    eqProfileCombo_.setSelectedId(newIdx + 1, juce::dontSendNotification);
    activeProfileIdx_ = newIdx;

    loadEQ(f);
    saveProfiles();
    logPanel_.log("EQ profile added: " + p.name);
}

void MainComponent::removeActiveEQProfile()
{
    if (activeProfileIdx_ < 0 || activeProfileIdx_ >= eqProfiles_.size()) return;

    eqProfiles_.remove(activeProfileIdx_);
    eqProfileCombo_.clear(juce::dontSendNotification);
    for (int i = 0; i < eqProfiles_.size(); ++i)
        eqProfileCombo_.addItem(eqProfiles_[i].name, i + 1);

    activeProfileIdx_ = -1;
    eqGraph_.clearPreset();
    eqStatusLabel_.setText("No EQ preset", juce::dontSendNotification);
    eqStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kMuted));

    saveProfiles();
}

void MainComponent::selectEQProfile(int idx)
{
    if (idx < 0 || idx >= eqProfiles_.size()) return;
    activeProfileIdx_ = idx;
    const auto& p = eqProfiles_[idx];

    // Restore EQ
    if (p.path.isNotEmpty()) {
        const juce::File f(p.path);
        if (f.existsAsFile()) {
            loadEQ(f);
        } else {
            eqStatusLabel_.setText("File not found: " + p.name,
                                   juce::dontSendNotification);
            eqStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kRed));
            logPanel_.log("EQ profile file missing: " + p.path, LogPanel::Level::Warn);
        }
    }

    // Restore raw FR
    eqGraph_.clearRawFR();
    lastRawFRPath_ = {};
    if (p.rawFRPath.isNotEmpty()) {
        const juce::File f(p.rawFRPath);
        if (f.existsAsFile()) loadRawFR(f);
    }

    // Restore target
    eqGraph_.clearTargetCurve();
    lastTargetPath_ = {};
    if (p.targetPath.isNotEmpty()) {
        const juce::File f(p.targetPath);
        if (f.existsAsFile()) loadTargetCurve(f);
    }

    // Restore norm freq + offset
    if (p.normFreqIdx >= 1 && p.normFreqIdx <= 6) {
        normFreqCombo_.setSelectedId(p.normFreqIdx, juce::sendNotification);
    }
    rawFROffsetSlider_.setValue(p.rawFROffset, juce::sendNotification);
}

void MainComponent::saveProfiles()
{
    if (!props_) return;
    props_->setValue("eqProfileCount", eqProfiles_.size());
    for (int i = 0; i < eqProfiles_.size(); ++i) {
        const auto pre = "eqProfile_" + juce::String(i) + "_";
        props_->setValue(pre + "name",        eqProfiles_[i].name);
        props_->setValue(pre + "path",        eqProfiles_[i].path);
        props_->setValue(pre + "rawFRPath",   eqProfiles_[i].rawFRPath);
        props_->setValue(pre + "targetPath",  eqProfiles_[i].targetPath);
        props_->setValue(pre + "rawFROffset", (double)eqProfiles_[i].rawFROffset);
        props_->setValue(pre + "normFreqIdx", eqProfiles_[i].normFreqIdx);
    }
    props_->saveIfNeeded();
}

void MainComponent::saveActiveProfile()
{
    if (activeProfileIdx_ < 0 || activeProfileIdx_ >= eqProfiles_.size()) return;
    auto& p       = eqProfiles_.getReference(activeProfileIdx_);
    p.rawFRPath   = lastRawFRPath_;
    p.targetPath  = lastTargetPath_;
    p.rawFROffset = (float)rawFROffsetSlider_.getValue();
    p.normFreqIdx = normFreqCombo_.getSelectedId();
    saveProfiles();
    logPanel_.log("Profile saved: " + p.name);
}

void MainComponent::restoreProfiles()
{
    if (!props_) return;
    const int count = props_->getIntValue("eqProfileCount", 0);
    for (int i = 0; i < count; ++i) {
        const auto pre = "eqProfile_" + juce::String(i) + "_";
        EQProfile p;
        p.name        = props_->getValue(pre + "name");
        p.path        = props_->getValue(pre + "path");
        p.rawFRPath   = props_->getValue(pre + "rawFRPath");
        p.targetPath  = props_->getValue(pre + "targetPath");
        p.rawFROffset = (float)props_->getDoubleValue(pre + "rawFROffset", 0.0);
        p.normFreqIdx = props_->getIntValue(pre + "normFreqIdx", 4);
        if (p.name.isNotEmpty()) {
            eqProfiles_.add(p);
            eqProfileCombo_.addItem(p.name, i + 1);
        }
    }
    // Auto-select last active profile (stored in legacy eqPath key)
    const auto lastEq = props_->getValue("eqPath");
    if (lastEq.isNotEmpty()) {
        for (int i = 0; i < eqProfiles_.size(); ++i) {
            if (eqProfiles_[i].path == lastEq) {
                eqProfileCombo_.setSelectedId(i + 1, juce::dontSendNotification);
                selectEQProfile(i);
                return;
            }
        }
        // Legacy path not in profiles — add it
        const juce::File f(lastEq);
        if (f.existsAsFile()) addEQProfile(f);
    }
}

std::vector<std::pair<float, float>> MainComponent::parseFRFile(const juce::File& f)
{
    juce::StringArray lines;
    f.readLines(lines);

    std::vector<std::pair<float, float>> points;
    for (const auto& line : lines) {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;
        // Skip header lines that start with letters (e.g. "Frequency,SPL")
        if (trimmed[0] >= 'A' && trimmed[0] <= 'z') continue;
        const auto parts = juce::StringArray::fromTokens(
            trimmed.replaceCharacter(',', ' '), " \t", "");
        if (parts.size() >= 2) {
            const float freq = parts[0].getFloatValue();
            const float db   = parts[1].getFloatValue();
            if (freq > 0.0f)
                points.emplace_back(freq, db);
        }
    }
    return points;
}

void MainComponent::loadRawFR(const juce::File& f)
{
    auto pts = parseFRFile(f);
    if (pts.empty()) {
        logPanel_.log("Raw FR: no valid data in " + f.getFileName(), LogPanel::Level::Warn);
        return;
    }
    lastRawFRPath_ = f.getFullPathName();
    eqGraph_.setRawFR(std::move(pts));
    logPanel_.log("Raw FR loaded: " + f.getFileName());
    if (props_) {
        props_->setValue("rawFRPath", lastRawFRPath_);
        props_->saveIfNeeded();
    }
}

void MainComponent::loadTargetCurve(const juce::File& f)
{
    auto pts = parseFRFile(f);
    if (pts.empty()) {
        logPanel_.log("Target curve: no valid data in " + f.getFileName(),
                      LogPanel::Level::Warn);
        return;
    }
    lastTargetPath_ = f.getFullPathName();
    eqGraph_.setTargetCurve(std::move(pts));
    logPanel_.log("Target loaded: " + f.getFileName());
    if (props_) {
        props_->setValue("targetPath", lastTargetPath_);
        props_->saveIfNeeded();
    }
}

} // namespace hearGOD
