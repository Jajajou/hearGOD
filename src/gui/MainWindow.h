#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

namespace hearGOD {

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name);
    ~MainWindow() override = default;

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace hearGOD
