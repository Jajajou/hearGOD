#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "HearGodAudioProcessor.h"
#include "EQGraph.h"
#include "MultiChannelMeter.h"
#include "HRIRPanel.h"
#include "LogPanel.h"
#include "SystemAudioSwitch.h"
#include "hearGOD/sofa_loader.h"
#include "hearGOD/peq_parser.h"
#include "hearGOD/nuols_engine.h"
#include "hearGOD/dfe_compensator.h"
#include "hearGOD/types.h"
#include <memory>

namespace hearGOD {

class MainComponent : public juce::Component
                    , private juce::Timer
                    , public juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioDeviceManager deviceManager_;
    HearGodAudioProcessor    processor_;

    // Left column
    HRIRPanel hrirPanel_;

    // Centre column
    EQGraph          eqGraph_;
    juce::ComboBox   eqProfileCombo_;
    juce::TextButton eqAddProfileBtn_;
    juce::TextButton eqRemoveProfileBtn_;
    juce::TextButton eqSaveProfileBtn_;
    juce::TextButton eqBrowseBtn_;
    juce::TextButton targetCurveBtn_;
    juce::TextButton rawFRBtn_;
    juce::ComboBox   normFreqCombo_;
    juce::Slider     rawFROffsetSlider_;
    juce::Label      rawFROffsetLabel_;
    juce::Label      eqStatusLabel_;
    std::unique_ptr<juce::FileChooser> eqFileChooser_;
    std::unique_ptr<juce::FileChooser> targetFileChooser_;
    std::unique_ptr<juce::FileChooser> rawFRFileChooser_;

    struct EQProfile {
        juce::String name;
        juce::String path;        // EQ .txt (may be empty)
        juce::String rawFRPath;
        juce::String targetPath;
        float rawFROffset = 0.0f;
        int normFreqIdx = 4;
    };
    juce::Array<EQProfile> eqProfiles_;
    int activeProfileIdx_ = -1;

    // Right column
    MultiChannelMeter multiMeter_;
    juce::Label       xrunLabel_;

    // Log panel (collapsible, bottom)
    LogPanel         logPanel_;
    juce::TextButton logToggleBtn_;
    bool             logVisible_ = false;

    // Header
    juce::ComboBox   inputDeviceCombo_;
    juce::ComboBox   outputDeviceCombo_;
    juce::TextButton modeBtn_;
    juce::Label      statusBar_;
    bool             stereoEqMode_ = false;

    int           lastXruns_      = 0;
    juce::String  lastSofaPath_;
    juce::String  lastEqPath_;
    juce::String  lastRawFRPath_;
    juce::String  lastTargetPath_;

    // Persistent GUI state
    juce::PropertiesFile::Options propsOptions_;
    std::unique_ptr<juce::PropertiesFile> props_;

    void saveState();
    void restoreState();

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void setupDeviceManager();
    void setupControls();
    void populateDeviceCombos();
    void applyDeviceSelection();
    void loadSOFA(const juce::File& f);
    void loadEQ(const juce::File& f);
    void addEQProfile(const juce::File& f);
    void removeActiveEQProfile();
    void selectEQProfile(int idx);
    void saveProfiles();
    void restoreProfiles();
    void saveActiveProfile();
    void loadTargetCurve(const juce::File& f);
    void loadRawFR(const juce::File& f);
    std::vector<std::pair<float, float>> parseFRFile(const juce::File& f);

    static constexpr int kLeftW   = 240;
    static constexpr int kRightW  = 160;
    static constexpr int kHeaderH = 52;
    static constexpr int kStatusH = 28;
};

} // namespace hearGOD
