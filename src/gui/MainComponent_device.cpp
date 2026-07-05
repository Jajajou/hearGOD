#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

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

} // namespace hearGOD
