#pragma once

#include "core/MidiRouter.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <string>
#include <vector>

namespace squeeze {

/// Hardware MIDI device layer. Wraps JUCE MidiInput, feeds messages into MidiRouter.
class MidiDeviceManager : public juce::MidiInputCallback {
public:
    explicit MidiDeviceManager(MidiRouter& router);
    ~MidiDeviceManager() override;

    MidiDeviceManager(const MidiDeviceManager&) = delete;
    MidiDeviceManager& operator=(const MidiDeviceManager&) = delete;

    // --- Control thread ---
    std::vector<std::string> getAvailableDevices() const;
    bool openDevice(const std::string& name, std::string& error);
    void closeDevice(const std::string& name);
    bool isDeviceOpen(const std::string& name) const;
    std::vector<std::string> getOpenDevices() const;
    void closeAllDevices();

    // --- JUCE MidiInputCallback (MIDI thread) ---
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    MidiRouter& router_;
    struct OpenDevice {
        std::unique_ptr<juce::MidiInput> device;
        std::string name;
    };
    std::vector<OpenDevice> openDevices_;
};

} // namespace squeeze
