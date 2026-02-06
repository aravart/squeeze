#include "core/Engine.h"
#include "core/Logger.h"
#include "core/MidiInputNode.h"
#include "core/PluginNode.h"

#include <unordered_map>
#include <unordered_set>

namespace squeeze {

Engine::Engine(Scheduler& scheduler)
    : scheduler_(scheduler)
{
    formatManager_.addDefaultFormats();
}

Engine::~Engine()
{
    stop();
    delete activeSnapshot_;
}

bool Engine::start(double sampleRate, int blockSize)
{
    SQ_LOG("start: sr=%.0f bs=%d", sampleRate, blockSize);

    // Try to open a real audio device
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.sampleRate = sampleRate;
    setup.bufferSize = blockSize;

    auto err = deviceManager_.initialise(0, 2, nullptr, true, {}, &setup);
    if (err.isEmpty())
    {
        deviceManager_.addAudioCallback(this);
        // audioDeviceAboutToStart has now fired, setting actual sr/bs
        // Rebuild snapshot with correct values
        updateGraph();
        SQ_LOG("start: audio device opened (sr=%.0f bs=%d)",
               sampleRate_.load(), blockSize_.load());
        return true;
    }

    // No device available (testing or headless)
    SQ_LOG("start: no audio device: %s", err.toRawUTF8());
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
    running_.store(true);
    return true;
}

void Engine::stop()
{
    SQ_LOG("stop");
    if (deviceManager_.getCurrentAudioDevice() != nullptr)
    {
        deviceManager_.removeAudioCallback(this);
        deviceManager_.closeAudioDevice();
    }
    running_.store(false);
}

bool Engine::isRunning() const { return running_.load(); }
double Engine::getSampleRate() const { return sampleRate_.load(); }
int Engine::getBlockSize() const { return blockSize_.load(); }

void Engine::prepareForTesting(double sampleRate, int blockSize)
{
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
}

Graph& Engine::getGraph()
{
    return graph_;
}

// ============================================================
// Plugin cache
// ============================================================

bool Engine::loadPluginCache(const std::string& xmlPath)
{
    return cache_.loadFromFile(juce::File(xmlPath));
}

std::vector<std::string> Engine::getAvailablePluginNames() const
{
    auto names = cache_.getAvailablePluginNames();
    std::vector<std::string> result;
    result.reserve(names.size());
    for (const auto& n : names)
        result.push_back(n.toStdString());
    return result;
}

const juce::PluginDescription* Engine::findPluginByName(const std::string& name) const
{
    return cache_.findByName(juce::String(name));
}

// ============================================================
// Node management
// ============================================================

int Engine::addNode(std::unique_ptr<Node> node, const std::string& name)
{
    Node* raw = node.get();
    int id = graph_.addNode(raw);
    ownedNodes_[id] = std::move(node);
    nodeNames_[id] = name;
    return id;
}

bool Engine::removeNode(int id)
{
    SQ_LOG("removeNode: id=%d", id);
    auto it = ownedNodes_.find(id);
    if (it == ownedNodes_.end())
        return false;

    graph_.removeNode(id);

    // Remove from midiDeviceNodes_ if present
    for (auto mit = midiDeviceNodes_.begin(); mit != midiDeviceNodes_.end(); ++mit)
    {
        if (mit->second == id)
        {
            midiDeviceNodes_.erase(mit);
            break;
        }
    }

    // Defer destruction for RT safety
    pendingDeletions_.push_back(std::move(it->second));
    ownedNodes_.erase(it);
    nodeNames_.erase(id);

    // Push updated graph so audio thread stops referencing removed node
    updateGraph();

    return true;
}

Node* Engine::getNode(int id) const
{
    return graph_.getNode(id);
}

std::string Engine::getNodeName(int id) const
{
    auto it = nodeNames_.find(id);
    return (it != nodeNames_.end()) ? it->second : "";
}

std::vector<std::pair<int, std::string>> Engine::getNodes() const
{
    std::vector<std::pair<int, std::string>> result;
    for (const auto& kv : ownedNodes_)
    {
        auto nameIt = nodeNames_.find(kv.first);
        std::string name = (nameIt != nodeNames_.end()) ? nameIt->second : "unknown";
        result.push_back({kv.first, name});
    }
    return result;
}

// ============================================================
// Plugin instantiation
// ============================================================

int Engine::addPlugin(const std::string& name, std::string& errorMessage)
{
    SQ_LOG("addPlugin: %s", name.c_str());
    auto* desc = cache_.findByName(juce::String(name));
    if (!desc)
    {
        errorMessage = "Plugin '" + name + "' not found in cache";
        return -1;
    }

    double sr = getSampleRate();
    int bs = getBlockSize();
    if (sr <= 0.0) sr = 44100.0;
    if (bs <= 0) bs = 512;

    juce::String juceError;
    auto pluginNode = PluginNode::create(*desc, formatManager_, sr, bs, juceError);
    if (!pluginNode)
    {
        errorMessage = "Failed to create plugin: " + juceError.toStdString();
        return -1;
    }

    return addNode(std::move(pluginNode), name);
}

// ============================================================
// MIDI input management
// ============================================================

std::vector<std::string> Engine::getAvailableMidiInputs() const
{
    std::vector<std::string> result;
    auto devices = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i)
        result.push_back(devices[i].name.toStdString());
    return result;
}

int Engine::addMidiInput(const std::string& deviceName, std::string& errorMessage)
{
    SQ_LOG("addMidiInput: %s", deviceName.c_str());

    auto midiNode = MidiInputNode::create(deviceName, errorMessage);
    if (!midiNode)
        return -1;

    int id = addNode(std::move(midiNode), deviceName);
    midiDeviceNodes_[deviceName] = id;
    return id;
}

void Engine::autoLoadMidiInputs()
{
    auto devices = juce::MidiInput::getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i)
    {
        std::string name = devices[i].name.toStdString();
        if (midiDeviceNodes_.find(name) == midiDeviceNodes_.end())
        {
            std::string err;
            int id = addMidiInput(name, err);
            if (id >= 0)
                SQ_LOG("autoLoadMidiInputs: loaded '%s' as node %d", name.c_str(), id);
            else
                SQ_LOG("autoLoadMidiInputs: failed '%s': %s", name.c_str(), err.c_str());
        }
    }
}

Engine::MidiRefreshResult Engine::refreshMidiInputs()
{
    MidiRefreshResult result;

    // Build set of currently available device names
    auto devices = juce::MidiInput::getAvailableDevices();
    std::unordered_set<std::string> currentDeviceNames;
    for (int i = 0; i < devices.size(); ++i)
        currentDeviceNames.insert(devices[i].name.toStdString());

    // Find new devices (available but not yet loaded)
    for (const auto& name : currentDeviceNames)
    {
        if (midiDeviceNodes_.find(name) == midiDeviceNodes_.end())
        {
            std::string err;
            int id = addMidiInput(name, err);
            if (id >= 0)
                result.added.push_back(name);
        }
    }

    // Find disappeared devices (loaded but no longer available)
    for (const auto& kv : midiDeviceNodes_)
    {
        if (currentDeviceNames.find(kv.first) == currentDeviceNames.end())
            result.removed.push_back(kv.first);
    }

    // Push updated graph if anything changed
    if (!result.added.empty())
        updateGraph();

    return result;
}

// ============================================================
// Graph topology
// ============================================================

int Engine::connect(int srcId, const std::string& srcPort,
                    int dstId, const std::string& dstPort, std::string& error,
                    int midiChannel)
{
    SQ_LOG("connect: %d:%s -> %d:%s (ch=%d)", srcId, srcPort.c_str(), dstId, dstPort.c_str(), midiChannel);
    PortAddress source{srcId, PortDirection::output, srcPort};
    PortAddress dest{dstId, PortDirection::input, dstPort};

    int connId = graph_.connect(source, dest, midiChannel);
    if (connId < 0)
    {
        error = graph_.getLastError();
        return -1;
    }

    return connId;
}

bool Engine::disconnect(int connId)
{
    SQ_LOG("disconnect: conn=%d", connId);
    return graph_.disconnect(connId);
}

std::vector<Connection> Engine::getConnections() const
{
    return graph_.getConnections();
}

// ============================================================
// Graph push
// ============================================================

void Engine::updateGraph()
{
    updateGraph(graph_);
}

void Engine::updateGraph(const Graph& graph)
{
    SQ_LOG("updateGraph: %d nodes", graph.getNodeCount());
    double sr = sampleRate_.load();
    int bs = blockSize_.load();

    auto* snapshot = buildSnapshot(graph, sr, bs);

    Command cmd;
    cmd.type = Command::Type::swapGraph;
    cmd.ptr = snapshot;
    scheduler_.sendCommand(cmd);
}

// ============================================================
// Parameters
// ============================================================

bool Engine::setParameter(int nodeId, const std::string& name, float value)
{
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return false;

    node->setParameter(name, value);
    return true;
}

float Engine::getParameter(int nodeId, const std::string& name) const
{
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return 0.0f;

    return node->getParameter(name);
}

std::vector<std::string> Engine::getParameterNames(int nodeId) const
{
    Node* node = graph_.getNode(nodeId);
    if (!node)
        return {};

    return node->getParameterNames();
}

// ============================================================
// Snapshot building
// ============================================================

GraphSnapshot* Engine::buildSnapshot(const Graph& graph,
                                     double sampleRate, int blockSize)
{
    auto order = graph.getExecutionOrder();
    auto connections = graph.getConnections();

    SQ_LOG("buildSnapshot: %d nodes, sr=%.0f bs=%d",
           (int)order.size(), sampleRate, blockSize);

    auto* snap = new GraphSnapshot();

    if (order.empty())
        return snap;

    // Map nodeId to index in slots
    std::unordered_map<int, int> idToIndex;
    for (int i = 0; i < (int)order.size(); ++i)
        idToIndex[order[i]] = i;

    // Determine max channel count for silence buffer
    int maxChannels = 2;

    for (int i = 0; i < (int)order.size(); ++i)
    {
        int nodeId = order[i];
        Node* node = graph.getNode(nodeId);

        int audioSrc = -1;
        std::vector<GraphSnapshot::NodeSlot::MidiSource> midiSources;

        // Find incoming connections for this node
        for (const auto& conn : connections)
        {
            if (conn.dest.nodeId != nodeId)
                continue;

            Node* srcNode = graph.getNode(conn.source.nodeId);
            if (!srcNode) continue;

            for (const auto& p : srcNode->getOutputPorts())
            {
                if (p.name == conn.source.portName)
                {
                    int srcIndex = idToIndex[conn.source.nodeId];
                    if (p.signalType == SignalType::audio)
                        audioSrc = srcIndex;
                    else if (p.signalType == SignalType::midi)
                        midiSources.push_back({srcIndex, conn.midiChannel});
                    break;
                }
            }
        }

        snap->slots.push_back({node, audioSrc, std::move(midiSources), false});

        // Determine output channel count from this node's output ports
        int outChannels = 2;
        for (const auto& p : node->getOutputPorts())
        {
            if (p.signalType == SignalType::audio)
            {
                outChannels = p.channels;
                break;
            }
        }

        if (outChannels > maxChannels)
            maxChannels = outChannels;

        snap->audioOutputs.emplace_back(outChannels, blockSize);
        snap->midiOutputs.emplace_back();
        snap->midiOutputs.back().ensureSize(256);
    }

    snap->silenceBuffer.setSize(maxChannels, blockSize);
    snap->silenceBuffer.clear();
    snap->filteredMidi.ensureSize(256);

    // Determine which slots are audio leaves (no other slot reads their audio output)
    std::unordered_set<int> hasAudioConsumer;
    for (const auto& slot : snap->slots)
    {
        if (slot.audioSourceIndex >= 0)
            hasAudioConsumer.insert(slot.audioSourceIndex);
    }

    for (int i = 0; i < (int)snap->slots.size(); ++i)
    {
        // A leaf is a node that has an audio output port but no one reads it
        bool hasAudioOutput = false;
        for (const auto& p : snap->slots[i].node->getOutputPorts())
        {
            if (p.signalType == SignalType::audio)
            {
                hasAudioOutput = true;
                break;
            }
        }
        snap->slots[i].isAudioLeaf = hasAudioOutput && !hasAudioConsumer.count(i);
    }

    return snap;
}

// ============================================================
// Audio processing
// ============================================================

void Engine::processBlock(juce::AudioBuffer<float>& outputBuffer,
                          juce::MidiBuffer& outputMidi,
                          int numSamples)
{
    // 1. Drain scheduler
    scheduler_.processPending([this](const Command& cmd) {
        switch (cmd.type)
        {
            case Command::Type::swapGraph:
            {
                auto* oldSnapshot = activeSnapshot_;
                activeSnapshot_ = static_cast<GraphSnapshot*>(cmd.ptr);
                SQ_LOG_RT("snapshot swap");
                if (oldSnapshot)
                    scheduler_.sendGarbage(GarbageItem::wrap(oldSnapshot));
                break;
            }
            case Command::Type::setParameter:
            {
                if (cmd.node)
                    cmd.node->setParameterByIndex(cmd.paramIndex, cmd.paramValue);
                break;
            }
        }
    });

    // 2. If no snapshot, output silence
    if (!activeSnapshot_ || activeSnapshot_->slots.empty())
    {
        outputBuffer.clear();
        outputMidi.clear();
        return;
    }

    auto& snap = *activeSnapshot_;

    // 3. Process each node in execution order
    for (int i = 0; i < (int)snap.slots.size(); ++i)
    {
        auto& slot = snap.slots[i];

        // Resolve inputs
        juce::AudioBuffer<float>& audioIn =
            (slot.audioSourceIndex >= 0)
                ? snap.audioOutputs[slot.audioSourceIndex]
                : snap.silenceBuffer;

        juce::MidiBuffer* midiInPtr = &snap.emptyMidi;

        if (slot.midiSources.size() == 1)
        {
            auto& ms = slot.midiSources[0];
            midiInPtr = &snap.midiOutputs[ms.slotIndex];

            if (ms.channelFilter > 0)
            {
                snap.filteredMidi.clear();
                for (const auto metadata : *midiInPtr)
                {
                    auto msg = metadata.getMessage();
                    if (msg.getChannel() == ms.channelFilter)
                        snap.filteredMidi.addEvent(msg, metadata.samplePosition);
                }
                midiInPtr = &snap.filteredMidi;
            }
        }
        else if (slot.midiSources.size() > 1)
        {
            snap.filteredMidi.clear();
            for (const auto& ms : slot.midiSources)
            {
                auto& srcBuf = snap.midiOutputs[ms.slotIndex];
                if (ms.channelFilter == 0)
                {
                    for (const auto metadata : srcBuf)
                        snap.filteredMidi.addEvent(metadata.getMessage(),
                                                   metadata.samplePosition);
                }
                else
                {
                    for (const auto metadata : srcBuf)
                    {
                        auto msg = metadata.getMessage();
                        if (msg.getChannel() == ms.channelFilter)
                            snap.filteredMidi.addEvent(msg, metadata.samplePosition);
                    }
                }
            }
            midiInPtr = &snap.filteredMidi;
        }

        // Clear outputs
        snap.audioOutputs[i].clear();
        snap.midiOutputs[i].clear();

        // Process
        ProcessContext ctx{audioIn, snap.audioOutputs[i],
                          *midiInPtr, snap.midiOutputs[i],
                          numSamples};
        slot.node->process(ctx);
    }

    // 4. Sum all audio leaf nodes to device output
    outputBuffer.clear();
    outputMidi.clear();

    for (int i = 0; i < (int)snap.slots.size(); ++i)
    {
        if (!snap.slots[i].isAudioLeaf)
            continue;

        auto& leafAudio = snap.audioOutputs[i];
        int chToAdd = std::min(outputBuffer.getNumChannels(),
                               leafAudio.getNumChannels());
        for (int ch = 0; ch < chToAdd; ++ch)
            for (int s = 0; s < numSamples; ++s)
                outputBuffer.addSample(ch, s, leafAudio.getSample(ch, s));
    }
}

// JUCE AudioIODeviceCallback
void Engine::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/, int /*numInputChannels*/,
    float* const* outputChannelData, int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    juce::AudioBuffer<float> output(outputChannelData, numOutputChannels, numSamples);
    juce::MidiBuffer midi;
    processBlock(output, midi, numSamples);
}

void Engine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate_.store(device->getCurrentSampleRate());
    blockSize_.store(device->getCurrentBufferSizeSamples());
    running_.store(true);
    SQ_LOG("audioDeviceAboutToStart: sr=%.0f bs=%d",
           device->getCurrentSampleRate(),
           device->getCurrentBufferSizeSamples());
}

void Engine::audioDeviceStopped()
{
    SQ_LOG("audioDeviceStopped");
    running_.store(false);
}

} // namespace squeeze
