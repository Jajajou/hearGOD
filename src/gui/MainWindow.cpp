#include "MainWindow.h"

namespace hearGOD {

MainWindow::MainWindow(const juce::String& name)
    : DocumentWindow(name,
                     juce::Colour(0xFF0D0D0F),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);
    setResizable(true, false);
    setResizeLimits(720, 480, 1920, 1200);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace hearGOD
