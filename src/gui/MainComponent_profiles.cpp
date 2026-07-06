#include "MainComponent.h"
#include <cmath>

namespace hearGOD {

static constexpr uint32_t kMuted = 0xFF71717A;
static constexpr uint32_t kRed   = 0xFFEF4444;

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

} // namespace hearGOD
