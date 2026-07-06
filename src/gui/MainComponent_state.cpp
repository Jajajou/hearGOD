#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

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

} // namespace hearGOD
