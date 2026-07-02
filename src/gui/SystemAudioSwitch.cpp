#include "SystemAudioSwitch.h"
#include <CoreAudio/CoreAudio.h>

namespace hearGOD {

static juce::String getDeviceName(AudioDeviceID deviceID)
{
    CFStringRef cfName = nullptr;
    UInt32 size = sizeof(cfName);
    AudioObjectPropertyAddress addr {
        kAudioDevicePropertyDeviceNameCFString,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectGetPropertyData(deviceID, &addr, 0, nullptr, &size, &cfName) != noErr
        || !cfName)
        return {};
    const juce::String name = juce::String::fromCFString(cfName);
    CFRelease(cfName);
    return name;
}

static juce::Array<AudioDeviceID> getAllOutputDevices()
{
    AudioObjectPropertyAddress addr {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    UInt32 size = 0;
    AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size);
    juce::Array<AudioDeviceID> devices;
    devices.resize((int)(size / sizeof(AudioDeviceID)));
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr,
                               &size, devices.getRawDataPointer());

    // Filter to output-capable devices
    juce::Array<AudioDeviceID> outputs;
    for (auto id : devices) {
        AudioObjectPropertyAddress outAddr {
            kAudioDevicePropertyStreamConfiguration,
            kAudioDevicePropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        UInt32 outSize = 0;
        if (AudioObjectGetPropertyDataSize(id, &outAddr, 0, nullptr, &outSize) == noErr
            && outSize > 0)
            outputs.add(id);
    }
    return outputs;
}

juce::String findDeviceName(const juce::String& prefix)
{
    for (auto id : getAllOutputDevices()) {
        const auto name = getDeviceName(id);
        if (name.startsWith(prefix))
            return name;
    }
    return {};
}

bool setSystemOutputDevice(const juce::String& deviceNamePrefix)
{
    for (auto id : getAllOutputDevices()) {
        const auto name = getDeviceName(id);
        if (name.startsWith(deviceNamePrefix)) {
            AudioObjectPropertyAddress addr {
                kAudioHardwarePropertyDefaultOutputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            const OSStatus err = AudioObjectSetPropertyData(
                kAudioObjectSystemObject, &addr, 0, nullptr,
                sizeof(id), &id);
            return err == noErr;
        }
    }
    return false;
}

} // namespace hearGOD
