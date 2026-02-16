#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::Logger::writeToLog("Squeeze v2 - JUCE " + juce::String(JUCE_MAJOR_VERSION)
                             + "." + juce::String(JUCE_MINOR_VERSION)
                             + "." + juce::String(JUCE_BUILDNUMBER));

    juce::AudioDeviceManager deviceManager;
    auto& deviceTypes = deviceManager.getAvailableDeviceTypes();
    juce::Logger::writeToLog("Audio device types: " + juce::String(deviceTypes.size()));
    for (auto* type : deviceTypes)
        juce::Logger::writeToLog("  - " + type->getTypeName());

    return 0;
}
