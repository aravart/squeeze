#pragma once

#include "core/Node.h"
#include "core/SPSCQueue.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <string>

namespace squeeze {

struct MidiEvent {
    uint8_t data[3];
    int size;           // 1, 2, or 3
};

class MidiInputNode : public Node, public juce::MidiInputCallback {
public:
    explicit MidiInputNode(const std::string& deviceName,
                           const juce::String& deviceIdentifier);
    ~MidiInputNode() override;

    /// Factory: find device by name, open it. Returns nullptr on failure.
    static std::unique_ptr<MidiInputNode> create(const std::string& deviceName,
                                                  std::string& errorMessage);

    // Node interface
    void prepare(double sampleRate, int blockSize) override;
    void process(ProcessContext& context) override;
    void release() override;
    std::vector<PortDescriptor> getInputPorts() const override;
    std::vector<PortDescriptor> getOutputPorts() const override;

    const std::string& getDeviceName() const;

    // juce::MidiInputCallback (called on MIDI thread)
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

private:
    std::string deviceName_;
    std::unique_ptr<juce::MidiInput> device_;
    SPSCQueue<MidiEvent, 1024> midiQueue_;
};

} // namespace squeeze
