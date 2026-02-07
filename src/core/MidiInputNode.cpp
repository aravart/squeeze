#include "core/MidiInputNode.h"
#include "core/Logger.h"

namespace squeeze {

MidiInputNode::MidiInputNode(const std::string& deviceName,
                             const juce::String& deviceIdentifier)
    : deviceName_(deviceName)
{
    device_ = juce::MidiInput::openDevice(deviceIdentifier, this);
    if (device_)
    {
        device_->start();
        SQ_LOG("MidiInputNode: opened device '%s'", deviceName_.c_str());
    }
    else
    {
        SQ_LOG("MidiInputNode: failed to open device '%s'", deviceName_.c_str());
    }
}

MidiInputNode::~MidiInputNode()
{
    if (device_)
    {
        device_->stop();
        SQ_LOG("MidiInputNode: closed device '%s'", deviceName_.c_str());
    }
}

std::unique_ptr<MidiInputNode> MidiInputNode::create(const std::string& deviceName,
                                                      std::string& errorMessage)
{
    auto devices = juce::MidiInput::getAvailableDevices();

    for (const auto& dev : devices)
    {
        if (dev.name.toStdString() == deviceName)
        {
            auto node = std::make_unique<MidiInputNode>(deviceName, dev.identifier);
            if (!node->device_)
            {
                errorMessage = "Failed to open MIDI device '" + deviceName + "'";
                return nullptr;
            }
            return node;
        }
    }

    errorMessage = "MIDI device '" + deviceName + "' not found";
    return nullptr;
}

void MidiInputNode::prepare(double /*sampleRate*/, int /*blockSize*/)
{
}

void MidiInputNode::process(ProcessContext& context)
{
    context.outputMidi.clear();

    MidiEvent event;
    while (midiQueue_.tryPop(event))
    {
        context.outputMidi.addEvent(event.data, event.size, 0);
    }
}

void MidiInputNode::release()
{
}

std::vector<PortDescriptor> MidiInputNode::getInputPorts() const
{
    return {};
}

std::vector<PortDescriptor> MidiInputNode::getOutputPorts() const
{
    return {{"midi_out", PortDirection::output, SignalType::midi, 1}};
}

const std::string& MidiInputNode::getDeviceName() const
{
    return deviceName_;
}

int MidiInputNode::getQueueFillLevel() const
{
    return midiQueue_.size();
}

int MidiInputNode::getDroppedCount() const
{
    return droppedCount_.load(std::memory_order_relaxed);
}

void MidiInputNode::handleIncomingMidiMessage(juce::MidiInput* /*source*/,
                                               const juce::MidiMessage& message)
{
    // Skip SysEx (too large for fixed-size queue entry)
    if (message.isSysEx())
        return;

    int size = message.getRawDataSize();
    if (size < 1 || size > 3)
        return;

    MidiEvent event;
    event.size = size;
    auto* raw = message.getRawData();
    for (int i = 0; i < size; ++i)
        event.data[i] = raw[i];

    // Drop on overflow (lock-free, no block)
    if (!midiQueue_.tryPush(event))
        droppedCount_.fetch_add(1, std::memory_order_relaxed);

    if (message.isNoteOn())
        SQ_LOG_RT_TRACE("MIDI [%s] note-on  ch=%d note=%d vel=%d",
                  deviceName_.c_str(), message.getChannel(),
                  message.getNoteNumber(), (int)message.getVelocity());
    else if (message.isNoteOff())
        SQ_LOG_RT_TRACE("MIDI [%s] note-off ch=%d note=%d",
                  deviceName_.c_str(), message.getChannel(),
                  message.getNoteNumber());
    else if (message.isController())
        SQ_LOG_RT_TRACE("MIDI [%s] cc       ch=%d cc=%d val=%d",
                  deviceName_.c_str(), message.getChannel(),
                  message.getControllerNumber(), message.getControllerValue());
    else if (message.isPitchWheel())
        SQ_LOG_RT_TRACE("MIDI [%s] pitch    ch=%d val=%d",
                  deviceName_.c_str(), message.getChannel(),
                  message.getPitchWheelValue());
    else if (message.isChannelPressure())
        SQ_LOG_RT_TRACE("MIDI [%s] pressure ch=%d val=%d",
                  deviceName_.c_str(), message.getChannel(),
                  message.getChannelPressureValue());
    else if (message.isProgramChange())
        SQ_LOG_RT_TRACE("MIDI [%s] pgm      ch=%d pgm=%d",
                  deviceName_.c_str(), message.getChannel(),
                  message.getProgramChangeNumber());
    else
        SQ_LOG_RT_TRACE("MIDI [%s] status=0x%02x size=%d",
                  deviceName_.c_str(), (unsigned)event.data[0], size);
}

} // namespace squeeze
