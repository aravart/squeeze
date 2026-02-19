#include "core/MidiDeviceManager.h"
#include "core/Logger.h"

#include <algorithm>
#include <cstring>

namespace squeeze {

MidiDeviceManager::MidiDeviceManager(MidiRouter& router)
    : router_(router)
{
    SQ_INFO("MidiDeviceManager: created");
}

MidiDeviceManager::~MidiDeviceManager()
{
    closeAllDevices();
    SQ_INFO("MidiDeviceManager: destroyed");
}

// ═══════════════════════════════════════════════════════════════════
// Control thread
// ═══════════════════════════════════════════════════════════════════

std::vector<std::string> MidiDeviceManager::getAvailableDevices() const
{
    auto devices = juce::MidiInput::getAvailableDevices();
    std::vector<std::string> names;
    names.reserve(static_cast<size_t>(devices.size()));
    for (const auto& d : devices)
        names.push_back(d.name.toStdString());
    SQ_DEBUG("MidiDeviceManager::getAvailableDevices: %d devices",
             static_cast<int>(names.size()));
    return names;
}

bool MidiDeviceManager::openDevice(const std::string& name, std::string& error)
{
    SQ_DEBUG("MidiDeviceManager::openDevice: %s", name.c_str());

    // Already open — no-op
    for (const auto& od : openDevices_)
    {
        if (od.name == name)
        {
            SQ_DEBUG("MidiDeviceManager::openDevice: already open: %s", name.c_str());
            return true;
        }
    }

    // Find the device in available devices
    auto available = juce::MidiInput::getAvailableDevices();
    juce::String identifier;
    bool found = false;
    for (const auto& d : available)
    {
        if (d.name.toStdString() == name)
        {
            identifier = d.identifier;
            found = true;
            break;
        }
    }

    if (!found)
    {
        error = "MIDI device not found: " + name;
        SQ_WARN("MidiDeviceManager::openDevice: %s", error.c_str());
        return false;
    }

    // Create device queue in MidiRouter before starting the device
    std::string queueError;
    router_.createDeviceQueue(name, queueError);

    // Open the JUCE MidiInput
    auto midiInput = juce::MidiInput::openDevice(identifier, this);
    if (!midiInput)
    {
        error = "Failed to open MIDI device: " + name;
        SQ_WARN("MidiDeviceManager::openDevice: %s", error.c_str());
        router_.removeDeviceQueue(name);
        router_.commit();
        return false;
    }

    midiInput->start();
    openDevices_.push_back({std::move(midiInput), name});

    // Publish device queue to audio thread routing table
    router_.commit();

    SQ_INFO("MidiDeviceManager: opened device: %s", name.c_str());
    return true;
}

void MidiDeviceManager::closeDevice(const std::string& name)
{
    SQ_DEBUG("MidiDeviceManager::closeDevice: %s", name.c_str());

    auto it = std::find_if(openDevices_.begin(), openDevices_.end(),
                           [&name](const OpenDevice& od) { return od.name == name; });
    if (it == openDevices_.end())
        return;

    it->device->stop();
    openDevices_.erase(it);

    router_.removeDeviceQueue(name);
    router_.commit();

    SQ_INFO("MidiDeviceManager: closed device: %s", name.c_str());
}

bool MidiDeviceManager::isDeviceOpen(const std::string& name) const
{
    for (const auto& od : openDevices_)
        if (od.name == name)
            return true;
    return false;
}

std::vector<std::string> MidiDeviceManager::getOpenDevices() const
{
    std::vector<std::string> names;
    names.reserve(openDevices_.size());
    for (const auto& od : openDevices_)
        names.push_back(od.name);
    return names;
}

void MidiDeviceManager::closeAllDevices()
{
    SQ_DEBUG("MidiDeviceManager::closeAllDevices: %d open",
             static_cast<int>(openDevices_.size()));

    for (auto& od : openDevices_)
    {
        od.device->stop();
        router_.removeDeviceQueue(od.name);
    }
    openDevices_.clear();
    router_.commit();
}

// ═══════════════════════════════════════════════════════════════════
// JUCE MidiInputCallback (MIDI thread — must be lock-free)
// ═══════════════════════════════════════════════════════════════════

void MidiDeviceManager::handleIncomingMidiMessage(juce::MidiInput* source,
                                                   const juce::MidiMessage& message)
{
    // Drop SysEx and oversized messages
    if (message.getRawDataSize() > 3)
        return;

    MidiEvent event;
    event.size = message.getRawDataSize();
    std::memcpy(event.data, message.getRawData(), static_cast<size_t>(event.size));

    // Find the device name for this source
    for (const auto& od : openDevices_)
    {
        if (od.device.get() == source)
        {
            router_.pushMidiEvent(od.name, event);
            return;
        }
    }
}

} // namespace squeeze
