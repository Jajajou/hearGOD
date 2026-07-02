#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "hearGOD/types.h"
#include <functional>
#include <vector>

namespace hearGOD {

struct ChannelMappingRow {
    const char* tag;
    const char* name;
    float azimuth;
    float elevation;
    bool  isLFE = false;
};

// Left panel: HRIR/SOFA section + channel mapping list + master gain knob.
class HRIRPanel : public juce::Component
{
public:
    std::function<void(const juce::File&)> onSofaLoaded;
    std::function<void(float dB)>          onMasterGainChanged;

    HRIRPanel();
    ~HRIRPanel() override = default;

    void showSOFAInfo(const juce::String& path, int measurementCount,
                      int irLength, int sampleRate);
    void showError(const juce::String& msg);
    void setChannelMapping(const std::vector<ChannelMappingRow>& rows);
    void setNumChannels(int n);
    void setMasterGain(float db);
    void setStereoMode(bool isStereo);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // SOFA section
    juce::TextButton browseBtn_;
    juce::Label      sofaNameLabel_;
    juce::Label      sofaMetaLabel_;
    juce::Label      sofaStatusLabel_;

    // Channel mapping list (drawn manually in paint)
    std::vector<ChannelMappingRow> rows_;
    int  numChannels_ = 16;
    bool stereoMode_  = false;

    // Master gain
    juce::Slider masterGainSlider_;
    juce::Label  masterGainValueLabel_;
    juce::Label  masterGainTitleLabel_;

    std::unique_ptr<juce::FileChooser> fileChooser_;

    void openFilePicker();
    void styleLabel(juce::Label& l, juce::Colour c, float sz, bool bold = false);

    static constexpr float kSofaH   = 88.0f;
    static constexpr float kRowH    = 22.0f;
    static constexpr float kGainH   = 90.0f;
};

} // namespace hearGOD
