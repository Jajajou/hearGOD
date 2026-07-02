#include <juce_gui_basics/juce_gui_basics.h>
#include "MainWindow.h"

class HearGodApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override
    {
        return JUCE_APPLICATION_NAME_STRING;
    }

    const juce::String getApplicationVersion() override
    {
        return JUCE_APPLICATION_VERSION_STRING;
    }

    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        mainWindow_ = std::make_unique<hearGOD::MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override {}

private:
    std::unique_ptr<hearGOD::MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(HearGodApplication)
