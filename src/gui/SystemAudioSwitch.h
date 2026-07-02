#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace hearGOD {

// Set macOS system default output device by name prefix.
// Returns true on success.
bool setSystemOutputDevice(const juce::String& deviceNamePrefix);

// Find exact device name matching prefix (for populating JUCE combo).
juce::String findDeviceName(const juce::String& prefix);

} // namespace hearGOD
