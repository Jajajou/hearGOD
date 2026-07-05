#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

static constexpr uint32_t kGreen = 0xFF22C55E;
static constexpr uint32_t kRed   = 0xFFEF4444;

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
